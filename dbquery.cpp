#include "dbquery.h"
#include "dbdecode.h"
using namespace std;

// ************* Basic query methods ***************

std::shared_ptr<pqxx::icursorstream> LiveNodesInBboxStart(pqxx::work *work, const string &tablePrefix, 
	const std::vector<double> &bbox, 
	const string &excludeTablePrefix)
{
	if(bbox.size() != 4)
		throw invalid_argument("Bbox has wrong length");
	string liveNodeTable = tablePrefix + "livenodes";
	string excludeTable;
	if(excludeTablePrefix.size() > 0)
		excludeTable = excludeTablePrefix + "nodeids";

	stringstream sql;
	sql.precision(9);
	sql << "SELECT "<<liveNodeTable<<".*, ST_X("<<liveNodeTable<<".geom) as lon, ST_Y("<<liveNodeTable<<".geom) AS lat";
	sql << " FROM ";
	sql << liveNodeTable;
	if(excludeTable.size() > 0)
		sql << " LEFT JOIN "<<excludeTable<<" ON "<<liveNodeTable<<".id = "<<excludeTable<<".id";
	sql << " WHERE "<<liveNodeTable<<".geom && ST_MakeEnvelope(";
	sql << bbox[0] <<","<< bbox[1] <<","<< bbox[2] <<","<< bbox[3] << ", 4326)";
	if(excludeTable.size() > 0)
		sql << " AND "<<excludeTable<<".id IS NULL";
	sql <<";";

	return std::shared_ptr<pqxx::icursorstream>(new pqxx::icursorstream( *work, sql.str(), "nodesinbbox", 1000 ));
}

int LiveNodesInBboxContinue(std::shared_ptr<pqxx::icursorstream> cursor, std::shared_ptr<IDataStreamHandler> enc)
{
	pqxx::icursorstream *c = cursor.get();
	return NodeResultsToEncoder(*c, enc);
}

void GetLiveWaysThatContainNodes(pqxx::work *work, const string &tablePrefix, 
	const string &excludeTablePrefix,
	const std::set<int64_t> &nodeIds, std::shared_ptr<IDataStreamHandler> enc)
{
	string wayTable = tablePrefix + "liveways";
	string wayMemTable = tablePrefix + "way_mems";
	int step = 1000;
	string excludeTable;
	if(excludeTablePrefix.size() > 0)
		excludeTable = excludeTablePrefix + "wayids";

	auto it=nodeIds.begin();
	while(it != nodeIds.end())
	{
		stringstream sqlFrags;
		int count = 0;
		for(; it != nodeIds.end() && count < step; it++)
		{
			if(count >= 1)
				sqlFrags << " OR ";
			sqlFrags << wayMemTable << ".member = " << *it;
			count ++;
		}

		string sql = "SELECT "+wayTable+".*";
		if(excludeTable.size() > 0)
			sql += ", "+excludeTable+".id";

		sql += " FROM "+wayMemTable+" INNER JOIN "+wayTable+" ON "\
			+wayMemTable+".id = "+wayTable+".id AND "+wayMemTable+".version = "+wayTable+".version";
		if(excludeTable.size() > 0)
			sql += " LEFT JOIN "+excludeTable+" ON "+wayTable+".id = "+excludeTable+".id";

		sql += " WHERE ("+sqlFrags.str()+")";
		if(excludeTable.size() > 0)
			sql += " AND "+excludeTable+".id IS NULL";
		sql += ";";

		pqxx::icursorstream cursor( *work, sql, "wayscontainingnodes", 1000 );	

		int records = 1;
		while (records>0)
			records = WayResultsToEncoder(cursor, enc);
	}
}

void GetLiveNodesById(pqxx::work *work, const string &tablePrefix, 
	const string &excludeTablePrefix, 
	const std::set<int64_t> &nodeIds, std::set<int64_t>::const_iterator &it, 
	size_t step, std::shared_ptr<IDataStreamHandler> enc)
{
	string nodeTable = tablePrefix + "livenodes";
	string excludeTable;
	if(excludeTablePrefix.size() > 0)
		excludeTable = excludeTablePrefix + "nodeids";

	stringstream sqlFrags;
	int count = 0;
	for(; it != nodeIds.end() && count < step; it++)
	{
		if(count >= 1)
			sqlFrags << " OR ";
		sqlFrags << nodeTable << ".id = " << *it;
		count ++;
	}

	string sql = "SELECT *, ST_X(geom) as lon, ST_Y(geom) AS lat";
	if(excludeTable.size() > 0)
		sql += ", "+excludeTable+".id";

	sql += " FROM "+ nodeTable;
	if(excludeTable.size() > 0)
		sql += " LEFT JOIN "+excludeTable+" ON "+nodeTable+".id = "+excludeTable+".id";

	sql += " WHERE ("+sqlFrags.str()+")";
	if(excludeTable.size() > 0)
		sql += " AND "+excludeTable+".id IS NULL";
	sql += ";";

	pqxx::icursorstream cursor( *work, sql, "nodecursor", 1000 );	

	count = 1;
	while(count > 0)
		count = NodeResultsToEncoder(cursor, enc);
}

void GetLiveRelationsForObjects(pqxx::work *work, const string &tablePrefix, 
	const std::string &excludeTablePrefix, 
	char qtype, const set<int64_t> &qids, 
	set<int64_t>::const_iterator &it, size_t step,
	const set<int64_t> &skipIds, 
	std::shared_ptr<IDataStreamHandler> enc)
{
	string relTable = tablePrefix + "liverelations";
	string relMemTable = tablePrefix + "relation_mems_" + qtype;
	string excludeTable;
	if(excludeTablePrefix.size() > 0)
		excludeTable = excludeTablePrefix + "relationids";

	stringstream sqlFrags;
	int count = 0;
	for(; it != qids.end() && count < step; it++)
	{
		if(count >= 1)
			sqlFrags << " OR ";
		sqlFrags << relMemTable << ".member = " << *it;
		count ++;
	}

	string sql = "SELECT "+relTable+".*";
	if(excludeTable.size() > 0)
		sql += ", "+excludeTable+".id";

	sql += " FROM "+relMemTable+" INNER JOIN "+relTable+" ON "+relMemTable+".id = "+relTable+".id AND "+relMemTable+".version = "+relTable+".version";
	if(excludeTable.size() > 0)
		sql += " LEFT JOIN "+excludeTable+" ON "+relTable+".id = "+excludeTable+".id";

	sql += " WHERE ("+sqlFrags.str()+")";
	if(excludeTable.size() > 0)
		sql += " AND "+excludeTable+".id IS NULL";
	sql += ";";

	pqxx::icursorstream cursor( *work, sql, "relationscontainingobjects", 1000 );	

	RelationResultsToEncoder(cursor, skipIds, enc);
}

void GetLiveWaysById(pqxx::work *work, const string &tablePrefix, 
	const std::string &excludeTablePrefix, 
	const std::set<int64_t> &wayIds, std::set<int64_t>::const_iterator &it, 
	size_t step, std::shared_ptr<IDataStreamHandler> enc)
{
	string wayTable = tablePrefix + "liveways";
	string excludeTable;
	if(excludeTablePrefix.size() > 0)
		excludeTable = excludeTablePrefix + "wayids";

	stringstream sqlFrags;
	int count = 0;
	for(; it != wayIds.end() && count < step; it++)
	{
		if(count >= 1)
			sqlFrags << " OR ";
		sqlFrags << wayTable << ".id = " << *it;
		count ++;
	}

	string sql = "SELECT *";
	if(excludeTable.size() > 0)
		sql += ", "+excludeTable+".id";

	sql += " FROM "+ wayTable;
	if(excludeTable.size() > 0)
		sql += " LEFT JOIN "+excludeTable+" ON "+wayTable+".id = "+excludeTable+".id";

	sql += " WHERE ("+sqlFrags.str()+")";
	if(excludeTable.size() > 0)
		sql += " AND "+excludeTable+".id IS NULL";
	sql += ";";

	pqxx::icursorstream cursor( *work, sql, "waycursor", 1000 );	

	set<int64_t> empty;
	int records = 1;
	while(records > 0)
		records = WayResultsToEncoder(cursor, enc);
}

void GetLiveRelationsById(pqxx::work *work, const string &tablePrefix, 
	const std::string &excludeTablePrefix, 
	const std::set<int64_t> &relationIds, std::set<int64_t>::const_iterator &it, 
	size_t step, std::shared_ptr<IDataStreamHandler> enc)
{
	string relationTable = tablePrefix + "liverelations";
	string excludeTable;
	if(excludeTablePrefix.size() > 0)
		excludeTable = excludeTablePrefix + "relationids";

	stringstream sqlFrags;
	int count = 0;
	for(; it != relationIds.end() && count < step; it++)
	{
		if(count >= 1)
			sqlFrags << " OR ";
		sqlFrags << relationTable << ".id = " << *it;
		count ++;
	}

	string sql = "SELECT *";
	if(excludeTable.size() > 0)
		sql += ", "+excludeTable+".id";

	sql += " FROM "+ relationTable;
	if(excludeTable.size() > 0)
		sql += " LEFT JOIN "+excludeTable+" ON "+relationTable+".id = "+excludeTable+".id";

	sql += " WHERE ("+sqlFrags.str()+")";
	if(excludeTable.size() > 0)
		sql += " AND "+excludeTable+".id IS NULL";
	sql += ";";

	pqxx::icursorstream cursor( *work, sql, "relationcursor", 1000 );	

	set<int64_t> empty;
	RelationResultsToEncoder(cursor, empty, enc);
}
