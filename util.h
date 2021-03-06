#ifndef _UTIL_H
#define _UTIL_H

#include <string>
#include <vector>
#include <map>
#include <rapidjson/reader.h> //rapidjson-dev
using namespace rapidjson;
#include <iostream>
using namespace std;
#include "cppGzip/DecodeGzip.h"
#include "cppGzip/EncodeGzip.h"
#include "cppo5m/OsmData.h"

int ReadFileContents(const char *filename, int binaryMode, std::string &contentOut);

std::vector<std::string> split(const std::string &s, char delim);

// http://rapidjson.org/md_doc_sax.html
class JsonToStringMap : public BaseReaderHandler<UTF8<>, JsonToStringMap>
{
protected:
	std::string currentKey;
public:
	map<std::string, std::string> tagMap; //Results
	bool verbose;

	JsonToStringMap() {
		verbose = false;
	}
	bool Null() { if(verbose) cout << "Null()" << endl; 
		return true; }
	bool Bool(bool b) { if(verbose) cout << "Bool(" << b << ")" << endl; 
		return true; }
	bool Int(int i) { if(verbose) cout << "Int(" << i << ")" << endl; 
		return true; }
	bool Uint(unsigned u) { if(verbose) cout << "Uint(" << u << ")" << endl; 
		return true; }
	bool Int64(int64_t i) { if(verbose) cout << "Int64(" << i << ")" << endl; 
		return true; }
	bool Uint64(uint64_t u) { if(verbose) cout << "Uint64(" << u << ")" << endl; 
		return true; }
	bool Double(double d) { if(verbose) cout << "Double(" << d << ")" << endl; 
		return true; }
	bool String(const char* str, SizeType length, bool copy) { 
		if(verbose)
			cout << "String(" << str << ", " << length << ", " << copy << ")" << endl;
		tagMap[currentKey] = str;
		return true;
	}
	bool StartObject() {
		if(verbose) 
			cout << "StartObject()" << endl; 
		tagMap.clear();
		return true; 
	}
	bool Key(const char* str, SizeType length, bool copy) { 
		if(verbose)
			cout << "Key(" << str << ", " << length << ", " << copy << ")" << endl;
		currentKey = str;
		return true;
	}
	bool EndObject(SizeType memberCount) { 
		if(verbose)
			cout << "EndObject(" << memberCount << ")" << endl; 
		return true; }
	bool StartArray() { 
		if(verbose)
			cout << "StartArray()" << endl; 
		return true; }
	bool EndArray(SizeType elementCount) { 
		if(verbose)
			cout << "EndArray(" << elementCount << ")" << endl; 
		return true; }
};

class JsonToWayMembers : public BaseReaderHandler<UTF8<>, JsonToWayMembers>
{
public:
	vector<int64_t> refs; //Results

	bool Null() { //cout << "Null()" << endl; 
		return true; }
	bool Bool(bool b) { //cout << "Bool(" << boolalpha << b << ")" << endl; 
		refs.push_back(b);
		return true; }
	bool Int(int i) { //cout << "Int(" << i << ")" << endl; 
		refs.push_back(i);
		return true; }
	bool Uint(unsigned u) { //cout << "Uint(" << u << ")" << endl; 
		refs.push_back(u);
		return true; }
	bool Int64(int64_t i) { //cout << "Int64(" << i << ")" << endl; 
		refs.push_back(i);
		return true; }
	bool Uint64(uint64_t u) { //cout << "Uint64(" << u << ")" << endl; 
		refs.push_back(u);
		return true; }
	bool Double(double d) { //cout << "Double(" << d << ")" << endl; 
		return true; }
	bool String(const char* str, SizeType length, bool copy) { 
		//cout << "String(" << str << ", " << length << ", " << boolalpha << copy << ")" << endl;
		return true;
	}
	bool StartObject() { 
		//cout << "StartObject()" << endl; 
		return true; 
	}
	bool Key(const char* str, SizeType length, bool copy) { 
		//cout << "Key(" << str << ", " << length << ", " << boolalpha << copy << ")" << endl;
		return true;
	}
	bool EndObject(SizeType memberCount) { 
		//cout << "EndObject(" << memberCount << ")" << endl; 
		return true; }
	bool StartArray() { 
		//cout << "StartArray()" << endl; 
		this->refs.clear();
		return true; }
	bool EndArray(SizeType elementCount) { 
		//cout << "EndArray(" << elementCount << ")" << endl; 
		return true; }
};

class JsonToRelMembers : public BaseReaderHandler<UTF8<>, JsonToRelMembers>
{
protected:
	string currentType;
	int64_t currentId;

public:
	//Results
	std::vector<std::string> refTypeStrs;
	std::vector<int64_t> refIds;
	//std::vector<std::string> refRoles;
	int depth;
	JsonToRelMembers()
	{
		depth = 0;
	}

	bool Null() { cout << "Null()" << endl; 
		return true; }
	bool Bool(bool b) { //cout << "Bool(" << boolalpha << b << ")" << endl; 
		return true; }
	bool Int(int i) { //cout << "Int(" << i << ")" << endl; 
		currentId = i;
		return true; }
	bool Uint(unsigned u) { //cout << "Uint(" << u << ")" << endl; 
		currentId = u;
		return true; }
	bool Int64(int64_t i) { //cout << "Int64(" << i << ")" << endl; 
		currentId = i;
		return true; }
	bool Uint64(uint64_t u) { //cout << "Uint64(" << u << ")" << endl; 
		currentId = u;
		return true; }
	bool Double(double d) { //cout << "Double(" << d << ")" << endl; 
		return true; }
	bool String(const char* str, SizeType length, bool copy) { 
		//cout << "String(" << str << ", " << length << ", " << boolalpha << copy << ")" << endl;
		currentType = str;
		return true;
	}
	bool StartObject() { 
		//cout << "StartObject()" << endl; 
		return true; 
	}
	bool Key(const char* str, SizeType length, bool copy) { 
		//cout << "Key(" << str << ", " << length << ", " << boolalpha << copy << ")" << endl;
		return true;
	}
	bool EndObject(SizeType memberCount) { 
		//cout << "EndObject(" << memberCount << ")" << endl; 
		return true; }
	bool StartArray() { 
		//cout << "StartArray()" << endl; 
		if(depth == 0)
		{
			currentType	= "";
			currentId = 0;
		}

		depth ++;
		return true; }
	bool EndArray(SizeType elementCount) { 
		//cout << "EndArray(" << elementCount << ")" << endl; 
		if(depth == 2)
		{
			refTypeStrs.push_back(currentType);
			refIds.push_back(currentId);
		}
		depth --;
		return true; }
};

class JsonToRelMemberRoles : public BaseReaderHandler<UTF8<>, JsonToRelMemberRoles>
{
public:
	//Results
	std::vector<std::string> refRoles;

	bool Null() { cout << "Null()" << endl; 
		return true; }
	bool Bool(bool b) { //cout << "Bool(" << boolalpha << b << ")" << endl; 
		return true; }
	bool Int(int i) { //cout << "Int(" << i << ")" << endl; 
		return true; }
	bool Uint(unsigned u) { //cout << "Uint(" << u << ")" << endl; 
		return true; }
	bool Int64(int64_t i) { //cout << "Int64(" << i << ")" << endl; 
		return true; }
	bool Uint64(uint64_t u) { //cout << "Uint64(" << u << ")" << endl; 
		return true; }
	bool Double(double d) { //cout << "Double(" << d << ")" << endl; 
		return true; }
	bool String(const char* str, SizeType length, bool copy) { 
		//cout << "String(" << str << ", " << length << ", " << boolalpha << copy << ")" << endl;
		refRoles.push_back(str);
		return true;
	}
	bool StartObject() { 
		//cout << "StartObject()" << endl; 
		return true; 
	}
	bool Key(const char* str, SizeType length, bool copy) { 
		//cout << "Key(" << str << ", " << length << ", " << boolalpha << copy << ")" << endl;
		return true;
	}
	bool EndObject(SizeType memberCount) { 
		//cout << "EndObject(" << memberCount << ")" << endl; 
		return true; }
	bool StartArray() { 
		//cout << "StartArray()" << endl; 
		refRoles.clear();
		return true; }
	bool EndArray(SizeType elementCount) { 
		//cout << "EndArray(" << elementCount << ")" << endl; 
		return true; }
};

bool ReadSettingsFile(const std::string &settingsPath, std::map<std::string, std::string> &configOut);
void WriteSettingsFile(const std::string &settingsPath, const std::map<std::string, std::string> &config);

void StrReplaceAll( string &s, const string &search, const string &replace );
std::string EscapeQuotes(std::string str);
std::string GeneratePgConnectionString(std::map<std::string, std::string> config);
void LoadOsmFromFile(const std::string &filename, std::shared_ptr<class IDataStreamHandler> csvStore);

double long2tilex(double lon, int z);
double lat2tiley(double lat, int z);
double tilex2long(int x, int z);
double tiley2lat(int y, int z);

void FindOuterBbox(const std::vector<std::vector<double> > &bboxesIn, std::vector<double> &bboxOut);

#endif //_UTIL_H

