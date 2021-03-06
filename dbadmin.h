#ifndef _DB_ADMIN_H
#define _DB_ADMIN_H

#include <pqxx/pqxx> //apt install libpqxx-dev
#include <string>
#include "pgcommon.h"

bool ResetActiveTables(pqxx::connection &c, pqxx::transaction_base *work, 
	const std::string &tableActivePrefix, 
	const std::string &tableStaticPrefix,
	std::string &errStr);

bool DbSetSchemaVersion(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &parentPrefix, 
	const std::string &tablePrefix, 
	int targetVer, bool latest, std::string &errStr);

bool DbCopyData(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &filePrefix,
	const std::string &tablePrefix, 
	std::string &errStr);

bool DbCreateIndices(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tablePrefix, 
	std::string &errStr);

bool DbRefreshMaxIds(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tableStaticPrefix, 
	const std::string &tableModPrefix, 
	const std::string &tableTestPrefix, 
	std::string &errStr);

bool DbRefreshMaxChangesetUid(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tableStaticPrefix, 
	const std::string &tableModPrefix, 
	const std::string &tableTestPrefix, 
	std::string &errStr);

bool DbGenerateUsernameTable(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tableStaticPrefix, 
	const std::string &tableModPrefix, 
	const std::string &tableTestPrefix, 
	std::string &errStr);

bool DbApplyDiffs(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tableStaticPrefix, 
	const std::string &tableModPrefix, 
	const std::string &tableTestPrefix, 
	const std::string &diffPath, 
	class PgCommon *pgCommon,
	std::string &errStr);

void DbCheckNodesExistForAllWays(pqxx::connection &c, pqxx::transaction_base *work, 
	const std::string &tablePrefix, 
	const std::string &excludeTablePrefix,
	const std::string &nodeStaticPrefix, 
	const std::string &nodeActivePrefix);

void DbCheckObjectIdTables(pqxx::connection &c, pqxx::transaction_base *work,
	const std::string &tablePrefix, const std::string &edition, const std::string &objType);

int DbUpdateWayBboxes(pqxx::connection &c, pqxx::transaction_base *work,
    int verbose,
	const std::string &tablePrefix, 
	class PgCommon *adminObj,
	std::string &errStr);

int DbUpdateRelationBboxes(pqxx::connection &c, pqxx::transaction_base *work,
    int verbose,
	const std::string &tablePrefix, 
	class PgCommon *adminObj,
	std::string &errStr);

bool DbCreateBboxIndices(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tablePrefix, 
	std::string &errStr);

bool DbDropBboxIndices(pqxx::connection &c, pqxx::transaction_base *work, 
	int verbose, 
	const std::string &tablePrefix, 
	std::string &errStr);

#endif //_DB_ADMIN_H

