#include <rapidjson/writer.h> //rapidjson-dev
#include <rapidjson/stringbuffer.h>
#include "dbstore.h"
#include "dbids.h"
#include "dbcommon.h"
using namespace std;

void EncodeTags(const TagMap &tagmap, string &out)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	for(auto it=tagmap.begin(); it!=tagmap.end(); it++)
	{
		writer.Key(it->first.c_str());
		writer.String(it->second.c_str());
	}
	writer.EndObject();
	out = buffer.GetString();
}

bool ObjectsToDatabase(pqxx::connection &c, pqxx::work *work, const string &tablePrefix, 
	const std::string &typeStr,
	const std::vector<const class OsmObject *> &objPtrs, 
	std::map<int64_t, int64_t> &createdNodeIds,
	map<string, int64_t> &nextIdMap,
	std::string &errStr,
	int verbose)
{
	char trueStr[] = "true";
	char falseStr[] = "true";
	auto it = nextIdMap.find(typeStr);
	int64_t &nextObjId = it->second;

	for(size_t i=0; i<objPtrs.size(); i++)
	{
		const class OsmObject *osmObject = objPtrs[i];
		const class OsmNode *nodeObject = dynamic_cast<const class OsmNode *>(osmObject);
		const class OsmWay *wayObject = dynamic_cast<const class OsmWay *>(osmObject);
		const class OsmRelation *relationObject = dynamic_cast<const class OsmRelation *>(osmObject);
		if(typeStr == "node" && nodeObject == nullptr)
			throw invalid_argument("Object type not node as expected");
		else if(typeStr == "way" && wayObject == nullptr)
			throw invalid_argument("Object type not way as expected");
		else if(typeStr == "relation" && relationObject == nullptr)
			throw invalid_argument("Object type not relation as expected");
		int64_t objId = osmObject->objId;
		int64_t version = osmObject->metaData.version;

		//Convert spatial and member data to appropriate formats
		stringstream wkt;
		stringstream refsJson;
		stringstream rolesJson;
		if(nodeObject != nullptr)
		{
			wkt.precision(9);
			wkt << "POINT(" << nodeObject->lon <<" "<< nodeObject->lat << ")";
		}
		else if(wayObject != nullptr)
		{
			refsJson << "[";
			for(size_t j=0;j < wayObject->refs.size(); j++)
			{
				if(j != 0)
					refsJson << ", ";
				refsJson << wayObject->refs[j];
			}
			refsJson << "]";
		}
		else if(relationObject != nullptr)
		{
			if(relationObject->refTypeStrs.size() != relationObject->refIds.size() || relationObject->refTypeStrs.size() != relationObject->refRoles.size())
				throw std::invalid_argument("Length of ref vectors must be equal");

			refsJson << "[";
			for(size_t j=0;j < relationObject->refIds.size(); j++)
			{
				if(j != 0)
					refsJson << ", ";
				refsJson << "[\"" << relationObject->refTypeStrs[j] << "\"," << relationObject->refIds[j] << "]";
			}
			refsJson << "]";

			rolesJson << "[";
			for(size_t j=0;j < relationObject->refRoles.size(); j++)
			{
				if(j != 0)
					rolesJson << ", ";
				rolesJson << "\"" << relationObject->refRoles[j] << "\"";
			}
			rolesJson << "]";
		}

		//Get existing object object in live table (if any)
		string checkExistingLiveSql = "SELECT * FROM "+ tablePrefix + "live"+typeStr+"s WHERE (id=$1);";
		pqxx::result r;
		try
		{
			if(verbose >= 1)
				cout << checkExistingLiveSql << " " << objId << endl;
			c.prepare(tablePrefix+"checkobjexists"+typeStr, checkExistingLiveSql);
			r = work->prepared(tablePrefix+"checkobjexists"+typeStr)(objId).exec();
		}
		catch (const pqxx::sql_error &e)
		{
			errStr = e.what();
			return false;
		}
		catch (const std::exception &e)
		{
			errStr = e.what();
			return false;
		}

		bool foundExisting = r.size() > 0;
		int64_t currentVersion = -1;
		if(foundExisting)
		{
			const pqxx::result::tuple row = r[0];
			currentVersion = row["version"].as<int64_t>();
		}

		//Get existing object version in old table (if any)
		string checkExistingOldSql = "SELECT MAX(version) FROM "+ tablePrefix + "old"+typeStr+"s WHERE (id=$1);";
		pqxx::result r2;
		try
		{
			if(verbose >= 1)
				cout << checkExistingOldSql << " " << objId << endl;
			c.prepare(tablePrefix+"checkoldobjexists"+typeStr, checkExistingOldSql);
			r2 = work->prepared(tablePrefix+"checkoldobjexists"+typeStr)(objId).exec();
		}
		catch (const pqxx::sql_error &e)
		{
			errStr = e.what();
			return false;
		}
		catch (const std::exception &e)
		{
			errStr = e.what();
			return false;
		}

		bool foundOld = false;
		int64_t oldVersion = -1;
		const pqxx::result::tuple row = r2[0];
		const pqxx::result::field field = row[0];
		if(!field.is_null())
		{
			oldVersion = field.as<int64_t>();
			foundOld = true;
		}

		//Check if we need to delete object from live table
		if(foundExisting && version >= currentVersion && !osmObject->metaData.visible)
		{
			string deletedLiveSql = "DELETE FROM "+ tablePrefix
				+ "live"+ typeStr +"s WHERE (id=$1);";
			try
			{
				if(verbose >= 1)
					cout << deletedLiveSql << " " << objId << endl;
				c.prepare(tablePrefix+"deletelive"+typeStr, deletedLiveSql);
				work->prepared(tablePrefix+"deletelive"+typeStr)(objId).exec();
			}
			catch (const pqxx::sql_error &e)
			{
				errStr = e.what();
				return false;
			}
			catch (const std::exception &e)
			{
				errStr = e.what();
				return false;
			}
		}

		//Check if we need to copy row from live to history table
		if(foundExisting && version > currentVersion)
		{
			const pqxx::result::tuple row = r[0];

			//Insert into live table
			stringstream ss;
			ss << "INSERT INTO "<< tablePrefix <<"old"<<typeStr<<"s (id, changeset, changeset_index, username, uid, timestamp, version, tags, visible";
			if(nodeObject != nullptr)
				ss << ", geom";
			else if(wayObject != nullptr)
				ss << ", members";
			else if(relationObject != nullptr)
				ss << ", members, memberroles";
			ss << ") VALUES ";
			ss << "($1,$2,$3,$4,$5,$6,$7,$8,$9";
			if(nodeObject != nullptr || wayObject != nullptr)
				ss << ",$10";
			else if (relationObject != nullptr)
				ss << ",$10,$11";
			ss << ") ON CONFLICT DO NOTHING;";

			try
			{
				if(nodeObject != nullptr)
				{
					if(verbose >= 1)
						cout << ss.str() << endl;
					c.prepare(tablePrefix+"copyoldnode", ss.str());

					pqxx::prepare::invocation invoc = work->prepared(tablePrefix+"copyoldnode");
					BindVal<int64_t>(invoc, row["id"]);
					BindVal<int64_t>(invoc, row["changeset"]);
					BindVal<int64_t>(invoc, row["changeset_index"]);
					BindVal<string>(invoc, row["username"]);
					BindVal<int64_t>(invoc, row["uid"]);
					BindVal<int64_t>(invoc, row["timestamp"]);
					BindVal<int64_t>(invoc, row["version"]);
					BindVal<string>(invoc, row["tags"]);
					invoc(true);
					BindVal<string>(invoc, row["geom"]);

					invoc.exec();
				}
				else if(wayObject != nullptr)
				{
					if(verbose >= 1)
						cout << ss.str() << endl;
					c.prepare(tablePrefix+"copyoldway", ss.str());

					pqxx::prepare::invocation invoc = work->prepared(tablePrefix+"copyoldway");
					BindVal<int64_t>(invoc, row["id"]);
					BindVal<int64_t>(invoc, row["changeset"]);
					BindVal<int64_t>(invoc, row["changeset_index"]);
					BindVal<string>(invoc, row["username"]);
					BindVal<int64_t>(invoc, row["uid"]);
					BindVal<int64_t>(invoc, row["timestamp"]);
					BindVal<int64_t>(invoc, row["version"]);
					BindVal<string>(invoc, row["tags"]);
					invoc(true);
					BindVal<string>(invoc, row["members"]);

					invoc.exec();
				}
				else if(relationObject != nullptr)
				{
					if(verbose >= 1)
						cout << ss.str() << endl;
					c.prepare(tablePrefix+"copyoldrelation", ss.str());
	
					pqxx::prepare::invocation invoc = work->prepared(tablePrefix+"copyoldrelation");
					BindVal<int64_t>(invoc, row["id"]);
					BindVal<int64_t>(invoc, row["changeset"]);
					BindVal<int64_t>(invoc, row["changeset_index"]);
					BindVal<string>(invoc, row["username"]);
					BindVal<int64_t>(invoc, row["uid"]);
					BindVal<int64_t>(invoc, row["timestamp"]);
					BindVal<int64_t>(invoc, row["version"]);
					BindVal<string>(invoc, row["tags"]);
					invoc(true);
					BindVal<string>(invoc, row["members"]);
					BindVal<string>(invoc, row["memberroles"]);

					invoc.exec();
				}
			}
			catch (const pqxx::sql_error &e)
			{
				stringstream ss2;
				ss2 << e.what() << ":" << e.query() << ":" << ss.str();
				errStr = ss2.str();
				return false;
			}
			catch (const std::exception &e)
			{
				stringstream ss2;
				ss2 << e.what() << ";" << ss.str() << endl;
				errStr = ss2.str();
				return false;
			}
		}

		//Check if this is the latest version and visible
		if(osmObject->metaData.visible && (!foundExisting || version >= currentVersion) && (!foundOld || version >= oldVersion))
		{
			if(!foundExisting)
			{
				//Insert into live table
				stringstream ss;
				ss << "INSERT INTO "<< tablePrefix <<"live"<<typeStr<<"s (id, changeset, username, uid, timestamp, version, tags";
				if(nodeObject != nullptr)
					ss << ", geom";
				else if(wayObject != nullptr)
					ss << ", members";
				else if(relationObject != nullptr)
					ss << ", members, memberroles";

				ss << ") VALUES ";

				string tagsJson;
				EncodeTags(osmObject->tags, tagsJson);

				if(objId <= 0)
				{
					if(osmObject->metaData.version != 1)
					{
						errStr = "Cannot assign a new node to any version but one.";
						return false;
					}

					//Assign a new ID
					objId = nextObjId;
					createdNodeIds[osmObject->objId] = nextObjId;
					nextObjId ++;
				}

				ss << "($1,$2,$3,$4,$5,$6,$7";
				if(nodeObject != nullptr)
					ss << ",ST_GeometryFromText($8, 4326)";
				else if(wayObject != nullptr)
					ss << ",$8";
				else if (relationObject != nullptr)
					ss << ",$8,$9";
				ss << ");";

				try
				{
					if(nodeObject != nullptr)
					{
						if(verbose >= 1)
							cout << ss.str() << endl;
						c.prepare(tablePrefix+"insertnode", ss.str());
						work->prepared(tablePrefix+"insertnode")(objId)(osmObject->metaData.changeset)(osmObject->metaData.username)\
							(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)(tagsJson)(wkt.str()).exec();
					}
					else if(wayObject != nullptr)
					{
						if(verbose >= 1)
							cout << ss.str() << endl;
						c.prepare(tablePrefix+"insertway", ss.str());
						work->prepared(tablePrefix+"insertway")(objId)(osmObject->metaData.changeset)(osmObject->metaData.username)\
							(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)(tagsJson)(refsJson.str()).exec();
					}
					else if(relationObject != nullptr)
					{
						if(verbose >= 1)
							cout << ss.str() << endl;
						c.prepare(tablePrefix+"insertrelation", ss.str());
						work->prepared(tablePrefix+"insertrelation")(objId)(osmObject->metaData.changeset)(osmObject->metaData.username)\
							(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)\
							(tagsJson)(refsJson.str())(rolesJson.str()).exec();
					}
				}
				catch (const pqxx::sql_error &e)
				{
					errStr = e.what();
					return false;
				}
				catch (const std::exception &e)
				{
					errStr = e.what();
					return false;
				}

				stringstream ssi;
				ssi << "INSERT INTO "<< tablePrefix << typeStr << "ids (id) VALUES ($1) ON CONFLICT DO NOTHING;";

				if(verbose >= 1)
					cout << ssi.str() << endl;
				c.prepare(tablePrefix+"insert"+typeStr+"ids", ssi.str());
				work->prepared(tablePrefix+"insert"+typeStr+"ids")(objId).exec();

			}
			else
			{
				//Update row in place
				stringstream ss;
				ss << "UPDATE "<< tablePrefix <<"live"<<typeStr<<"s SET changeset=$1, username=$2, uid=$3, timestamp=$4, version=$5, tags=$6";
				if(nodeObject != nullptr)
					ss << ", geom=ST_GeometryFromText($8, 4326)";
				else if(wayObject != nullptr)
					ss << ", members=$8";
				else if(relationObject != nullptr)
					ss << ", members=$8, memberroles=$9";
				ss << " WHERE id = $7;";
				string tagsJson;
				EncodeTags(osmObject->tags, tagsJson);

				if(objId < 0)
				{
					errStr = "We should never have to assign an ID and SQL update the live table.";
					return false;
				}

				try
				{
					if(nodeObject != nullptr)
					{
						if(verbose >= 1)
							cout << ss.str() << endl;
						c.prepare(tablePrefix+"updatenode", ss.str());
						work->prepared(tablePrefix+"updatenode")(osmObject->metaData.changeset)(osmObject->metaData.username)\
							(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)(tagsJson)(objId)(wkt.str()).exec();
					}
					else if(wayObject != nullptr)
					{
						if(verbose >= 1)
							cout << ss.str() << endl;
						c.prepare(tablePrefix+"updateway", ss.str());
						work->prepared(tablePrefix+"updateway")(osmObject->metaData.changeset)(osmObject->metaData.username)\
							(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)(tagsJson)(objId)(refsJson.str()).exec();
					}
					else if(relationObject != nullptr)
					{
						if(verbose >= 1)
							cout << ss.str() << endl;
						c.prepare(tablePrefix+"updaterelation", ss.str());
						work->prepared(tablePrefix+"updaterelation")(osmObject->metaData.changeset)(osmObject->metaData.username)\
							(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)\
							(tagsJson)(objId)(refsJson.str())(rolesJson.str()).exec();
					}

				}
				catch (const pqxx::sql_error &e)
				{
					errStr = e.what();
					return false;
				}
				catch (const std::exception &e)
				{
					errStr = e.what();
					return false;
				}
			}
		}
		else
		{
			//Insert into history table
			stringstream ss;
			ss << "INSERT INTO "<< tablePrefix <<"old"<<typeStr<<"s (id, changeset, username, uid, timestamp, version, tags, visible";
			if(nodeObject != nullptr)
				ss << ", geom";
			else if(wayObject != nullptr)
				ss << ", members";
			else if(relationObject != nullptr)
				ss << ", members, memberroles";
			ss << ") VALUES ";
			string tagsJson;
			EncodeTags(osmObject->tags, tagsJson);

			if(objId <= 0)
			{
				if(osmObject->metaData.version != 1)
				{
					errStr = "Cannot assign a new node to any version but 1.";
					return false;
				}

				//Assign a new ID
				objId = nextObjId;
				createdNodeIds[osmObject->objId] = nextObjId;
				nextObjId ++;
			}

			ss << "($1,$2,$3,$4,$5,$6,$7,$8";
			if(nodeObject != nullptr)
				ss << ",ST_GeometryFromText($9, 4326)";
			else if(wayObject != nullptr)
				ss << ",$9";
			else if(relationObject != nullptr)
				ss << ",$9,$10";
			ss << ") ON CONFLICT DO NOTHING;";

			try
			{	
				if(nodeObject != nullptr)
				{
					if(verbose >= 1)
						cout << ss.str() << endl;
					c.prepare(tablePrefix+"insertoldnode", ss.str());
					work->prepared(tablePrefix+"insertoldnode")(objId)(osmObject->metaData.changeset)(osmObject->metaData.username)\
						(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)\
						(tagsJson)(osmObject->metaData.visible)(wkt.str()).exec();
				}
				else if(wayObject != nullptr)
				{
					if(verbose >= 1)
						cout << ss.str() << endl;
					c.prepare(tablePrefix+"insertoldway", ss.str());
					work->prepared(tablePrefix+"insertoldway")(objId)(osmObject->metaData.changeset)(osmObject->metaData.username)\
						(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)\
						(tagsJson)(osmObject->metaData.visible)(refsJson.str()).exec();
				}
				else if(relationObject != nullptr)
				{
					if(verbose >= 1)
						cout << ss.str() << endl;
					c.prepare(tablePrefix+"insertoldrelation", ss.str());
					work->prepared(tablePrefix+"insertoldrelation")(objId)(osmObject->metaData.changeset)(osmObject->metaData.username)\
						(osmObject->metaData.uid)(osmObject->metaData.timestamp)(osmObject->metaData.version)\
						(tagsJson)(osmObject->metaData.visible)(refsJson.str())(rolesJson.str()).exec();
				}

			}
			catch (const pqxx::sql_error &e)
			{
				stringstream ss2;
				ss2 << e.what() << ":" << e.query() << ":" << ss.str();
				errStr = ss2.str();
				return false;
			}
			catch (const std::exception &e)
			{
				stringstream ss2;
				ss2 << e.what() << ";" << ss.str() << endl;
				errStr = ss2.str();
				return false;
			}

			stringstream ssi;
			ssi << "INSERT INTO "<< tablePrefix << typeStr << "ids (id) VALUES ($1) ON CONFLICT DO NOTHING;";

			if(verbose >= 1)
				cout << ssi.str() << endl;
			c.prepare(tablePrefix+"insert"+typeStr+"ids", ssi.str());
			work->prepared(tablePrefix+"insert"+typeStr+"ids")(objId).exec();
		}

		if(wayObject != nullptr)
		{
			//Update way member table
			size_t j=0;
			while(j < wayObject->refs.size())
			{
				stringstream sswm;
				size_t insertSize = 0;
				sswm << "INSERT INTO "<< tablePrefix << "way_mems (id, version, index, member) VALUES ";

				size_t initialj = j;
				for(; j<wayObject->refs.size() && insertSize < 1000; j++)
				{
					if(j!=initialj)
						sswm << ",";
					sswm << "("<<objId<<","<<wayObject->metaData.version<<","<<j<<","<<wayObject->refs[j]<<")";
					insertSize ++;
				}
				sswm << ";";

				try
				{
					if(verbose >= 1)
						cout << sswm.str() << endl;
					work->exec(sswm.str());
				}
				catch (const pqxx::sql_error &e)
				{
					errStr = e.what();
					return false;
				}
				catch (const std::exception &e)
				{
					errStr = e.what();
					return false;
				}
			}
		}
		else if(relationObject != nullptr)
		{
			//Update relation member tables
			for(size_t j=0;j < relationObject->refIds.size(); j++)
			{
				stringstream ssrm;
				ssrm << "INSERT INTO "<< tablePrefix << "relation_mems_"<< relationObject->refTypeStrs[j][0] << " (id, version, index, member) VALUES ";
				ssrm << "("<<objId<<","<<relationObject->metaData.version<<","<<j<<","<<relationObject->refIds[j]<<");";

				try
				{
					if(verbose >= 1)
						cout << ssrm.str() << endl;
					work->exec(ssrm.str());
				}
				catch (const pqxx::sql_error &e)
				{
					errStr = e.what();
					return false;
				}
				catch (const std::exception &e)
				{
					errStr = e.what();
					return false;
				}
			}
		}
	}

	return true;
}

bool StoreObjects(pqxx::connection &c, pqxx::work *work, 
	const string &tablePrefix, 
	class OsmData osmData, 
	std::map<int64_t, int64_t> &createdNodeIds, 
	std::map<int64_t, int64_t> &createdWayIds,
	std::map<int64_t, int64_t> &createdRelationIds,
	std::string &errStr)
{
	map<string, int64_t> nextIdMapOriginal, nextIdMap;
	bool ok = GetNextObjectIds(work, tablePrefix, nextIdMapOriginal, errStr);
	if(!ok)
		return false;
	nextIdMap = nextIdMapOriginal;

	//Store nodes
	std::vector<const class OsmObject *> objPtrs;
	for(size_t i=0; i<osmData.nodes.size(); i++)
		objPtrs.push_back(&osmData.nodes[i]);
	ok = ObjectsToDatabase(c, work, tablePrefix, "node", objPtrs, createdNodeIds, nextIdMap, errStr, 0);
	if(!ok)
		return false;

	//Update numbering for created nodes used in ways and relations
	for(size_t i=0; i<osmData.ways.size(); i++)
	{
		class OsmWay &way = osmData.ways[i];
		for(size_t j=0; j<way.refs.size(); j++)
		{
			if(way.refs[j] > 0) continue;
			std::map<int64_t, int64_t>::iterator it = createdNodeIds.find(way.refs[j]);
			if(it == createdNodeIds.end())
			{
				stringstream ss;
				ss << "Way "<< way.objId << " depends on undefined node " << way.refs[j];
				errStr = ss.str();
				return false;
			}
			way.refs[j] = it->second;
		}
	}

	for(size_t i=0; i<osmData.relations.size(); i++)
	{
		class OsmRelation &rel = osmData.relations[i];
		for(size_t j=0; j<rel.refIds.size(); j++)
		{
			if(rel.refTypeStrs[j] != "node" or rel.refIds[j] > 0) continue;
			std::map<int64_t, int64_t>::iterator it = createdNodeIds.find(rel.refIds[j]);
			if(it == createdNodeIds.end())
			{
				stringstream ss;
				ss << "Relation "<< rel.objId << " depends on undefined node " << rel.refIds[j];
				errStr = ss.str();
				return false;
			}
			rel.refIds[j] = it->second;
		}
	}

	//Store ways
	objPtrs.clear();
	for(size_t i=0; i<osmData.ways.size(); i++)
		objPtrs.push_back(&osmData.ways[i]);
	ok = ObjectsToDatabase(c, work, tablePrefix, "way", objPtrs, createdWayIds, nextIdMap, errStr, 0);
	if(!ok)
		return false;

	//Update numbering for created ways used in relations
	for(size_t i=0; i<osmData.relations.size(); i++)
	{
		class OsmRelation &rel = osmData.relations[i];
		for(size_t j=0; j<rel.refIds.size(); j++)
		{
			if(rel.refTypeStrs[j] != "way" or rel.refIds[j] > 0) continue;
			std::map<int64_t, int64_t>::iterator it = createdWayIds.find(rel.refIds[j]);
			if(it == createdNodeIds.end())
			{
				stringstream ss;
				ss << "Relation "<< rel.objId << " depends on undefined way " << rel.refIds[j];
				errStr = ss.str();
				return false;
			}
			rel.refIds[j] = it->second;
		}
	}

	//Store relations one by one (since one relation can depend on another)		
	for(size_t i=0; i<osmData.relations.size(); i++)
	{
		//Check refs for the relation we are about to add
		class OsmRelation &rel = osmData.relations[i];
		for(size_t j=0; j<rel.refIds.size(); j++)
		{
			if(rel.refTypeStrs[j] != "relation" or rel.refIds[j] > 0) continue;
			std::map<int64_t, int64_t>::iterator it = createdRelationIds.find(rel.refIds[j]);
			if(it == createdNodeIds.end())
			{
				stringstream ss;
				ss << "Relation "<< rel.objId << " depends on undefined way " << rel.refIds[j];
				errStr = ss.str();
				return false;
			}
			rel.refIds[j] = it->second;
		}

		//Add to database
		objPtrs.clear();
		objPtrs.push_back(&osmData.relations[i]);

		ok = ObjectsToDatabase(c, work, tablePrefix, "relation", objPtrs, createdRelationIds, nextIdMap, errStr, 0);
		if(!ok)
			return false;
	}

	ok = UpdateNextObjectIds(work, tablePrefix, nextIdMap, nextIdMapOriginal, errStr);
	if(!ok)
		return false;
	return true;
}
