/*-------------------------------------------------------------------------
 *
 * clickhouse_fdw.h
 *		  Foreign-data wrapper for remote Clickhouse servers
 *
 * IDENTIFICATION
 *		  contrib/clickhouedb_fdw/clickhouse_fdw.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef POSTGRES_FDW_H
#define POSTGRES_FDW_H

#include "foreign/foreign.h"
#include "lib/stringinfo.h"
#include "nodes/relation.h"
#include "utils/relcache.h"
#include "catalog/pg_operator.h"

typedef void *ch_connection;

typedef enum {
	CH_DEFAULT,
	CH_COLLAPSING_MERGE_TREE
} CHRemoteTableEngine;

/*
 * FDW-specific planner information kept in RelOptInfo.fdw_private for a
 * postgres_fdw foreign table.  For a baserel, this struct is created by
 * postgresGetForeignRelSize, although some fields are not filled till later.
 * postgresGetForeignJoinPaths creates it for a joinrel, and
 * postgresGetForeignUpperPaths creates it for an upperrel.
 */
typedef struct CHFdwRelationInfo
{
	/*
	 * True means that the relation can be pushed down. Always true for simple
	 * foreign scan.
	 */
	bool		pushdown_safe;

	/*
	 * Restriction clauses, divided into safe and unsafe to pushdown subsets.
	 * All entries in these lists should have RestrictInfo wrappers; that
	 * improves efficiency of selectivity and cost estimation.
	 */
	List	   *remote_conds;
	List	   *local_conds;

	/* Actual remote restriction clauses for scan (sans RestrictInfos) */
	List	   *final_remote_exprs;

	/* Bitmap of attr numbers we need to fetch from the remote server. */
	Bitmapset  *attrs_used;

	/* Cost and selectivity of local_conds. */
	QualCost	local_conds_cost;
	Selectivity local_conds_sel;

	/* Selectivity of join conditions */
	Selectivity joinclause_sel;

	/* Estimated size and cost for a scan or join. */
	double		rows;
	int			width;
	Cost		startup_cost;
	Cost		total_cost;
	/* Costs excluding costs for transferring data from the foreign server */
	Cost		rel_startup_cost;
	Cost		rel_total_cost;

	/* Options extracted from catalogs. */
	bool		use_remote_estimate;
	Cost		fdw_startup_cost;
	Cost		fdw_tuple_cost;
	List	   *shippable_extensions;	/* OIDs of whitelisted extensions */

	/* Cached catalog information. */
	ForeignTable *table;
	ForeignServer *server;
	UserMapping *user;			/* only set in use_remote_estimate mode */

	int			fetch_size;		/* fetch size for this remote table */

	/*
	 * Name of the relation while EXPLAINing ForeignScan. It is used for join
	 * relations but is set for all relations. For join relation, the name
	 * indicates which foreign tables are being joined and the join type used.
	 */
	StringInfo	relation_name;

	/* Join information */
	RelOptInfo *outerrel;
	RelOptInfo *innerrel;
	JoinType	jointype;
	/* joinclauses contains only JOIN/ON conditions for an outer join */
	List	   *joinclauses;	/* List of RestrictInfo */

	/* Grouping information */
	List	   *grouped_tlist;

	/* Subquery information */
	bool		make_outerrel_subquery; /* do we deparse outerrel as a
                                         * subquery? */
	bool		make_innerrel_subquery; /* do we deparse innerrel as a
                                         * subquery? */
	Relids		lower_subquery_rels;	/* all relids appearing in lower
                                         * subqueries */

	/*
	 * Index of the relation.  It is used to create an alias to a subquery
	 * representing the relation.
	 */
	int			relation_index;

	/* Custom */
	CHRemoteTableEngine		ch_table_engine;
	char					ch_table_sign_field[NAMEDATALEN];
} CHFdwRelationInfo;

/* in clickhouse_fdw.c */
extern int	set_transmission_modes(void);
extern void reset_transmission_modes(int nestlevel);
extern ForeignServer *get_foreign_server(Relation rel);

/* in connection.c */
extern ch_connection GetConnection(UserMapping *user, bool will_prep_stmt, bool read);
extern void ReleaseConnection(ch_connection conn);
extern unsigned int GetCursorNumber(ch_connection conn);
extern unsigned int GetPrepStmtNumber(ch_connection conn);
extern void chfdw_exec_query(ch_connection conn, const char *query);
extern void chfdw_report_error(int elevel, ch_connection conn,
                               bool clear, const char *sql);

/* in option.c */
extern void
ExtractConnectionOptions(List *defelems, char **driver, char **host, int *port,
                         char **dbname, char **username, char **password);

extern List *ExtractExtensionList(const char *extensionsString,
                                  bool warnOnMissing);

/* in deparse.c */
extern void classifyConditions(PlannerInfo *root,
                               RelOptInfo *baserel,
                               List *input_conds,
                               List **remote_conds,
                               List **local_conds);
extern bool is_foreign_expr(PlannerInfo *root,
                            RelOptInfo *baserel,
                            Expr *expr);
extern void deparseInsertSql(StringInfo buf, RangeTblEntry *rte,
                             Index rtindex, Relation rel,
                             List *targetAttrs, bool doNothing, List *returningList,
                             List **retrieved_attrs);
extern void deparseAnalyzeSizeSql(StringInfo buf, Relation rel);
extern void deparseAnalyzeSql(StringInfo buf, Relation rel,
                              List **retrieved_attrs);
extern Expr *find_em_expr_for_rel(EquivalenceClass *ec, RelOptInfo *rel);
extern List *build_tlist_to_deparse(RelOptInfo *foreignrel);
extern void deparseSelectStmtForRel(StringInfo buf, PlannerInfo *root,
                                    RelOptInfo *foreignrel, List *tlist,
                                    List *remote_conds, List *pathkeys, bool is_subquery,
                                    List **retrieved_attrs, List **params_list);
extern const char *get_jointype_name(JoinType jointype);

/* in shippable.c */
extern bool is_builtin(Oid objectId);
extern bool is_shippable(Oid objectId, Oid classId, CHFdwRelationInfo *fpinfo);
extern int is_equal_op(Oid opno);

/*
 * Connection cache hash table entry
 */
typedef struct ConnCacheKey
{
	Oid		userid;
	bool    read;
} ConnCacheKey;

typedef struct ConnCacheEntry
{
	ConnCacheKey	key;			/* hash key (must be first) */
	ch_connection	conn;			/* connection to foreign server, or NULL */
	/* Remaining fields are invalid when conn is NULL: */
	int			xact_depth;		/* 0 = no xact open, 1 = main xact open, 2 =
                                 * one level of subxact open, etc */
	bool		have_error;		/* have any subxacts aborted in this xact? */
	bool		changing_xact_state;	/* xact state change in process */
	bool		invalidated;	/* true if reconnect is pending */
	bool		read;				/* Separate entry for read/write */
	uint32		server_hashvalue;	/* hash value of foreign server OID */
	uint32		mapping_hashvalue;	/* hash value of user mapping OID */
} ConnCacheEntry;

/* libclickhouse_link.c */
typedef struct
{
	void	*query_response;
	void	*read_state;
	char	*query;
} ch_cursor;

typedef ch_connection (*connect_method)(char *connstring);
typedef void (*disconnect_method)(ch_connection conn);
typedef void (*check_conn_method)(const char *password, UserMapping *user);
typedef ch_cursor *(*simple_query_method)(ch_connection conn, const char *query);
typedef void (*simple_insert_method)(ch_connection conn, const char *query);
typedef void (*cursor_free_method)(ch_cursor *cursor);
typedef char **(*cursor_fetch_row_method)(ch_cursor *cursor, size_t attcount);
typedef text *(*cursor_fetch_raw_data)(ch_cursor *cursor);

typedef struct
{
	connect_method				connect;
	disconnect_method			disconnect;
	simple_query_method			simple_query;
	simple_insert_method		simple_insert;
	cursor_free_method			cursor_free;
	cursor_fetch_row_method		fetch_row;
	cursor_fetch_raw_data		fetch_raw_data;
} libclickhouse_methods;

extern libclickhouse_methods *clickhouse_gate;

/* Custom behavior types */
typedef enum {
	CF_USUAL = 0,
	CF_UNSHIPPABLE,		/* do not ship */
	CF_SIGN_SUM,		/* SUM aggregation */
	CF_SIGN_AVG,		/* AVG aggregation */
	CF_SIGN_COUNT,		/* COUNT aggregation */
	CF_ISTORE_TYPE,		/* istore type */
	CF_ISTORE_SUM,		/* SUM on istore column */
	CF_ISTORE_SUM_UP,	/* SUM_UP on istore column */
	CF_ISTORE_ARR,		/* COLUMN splitted to array */
	CF_ISTORE_COL,		/* COLUMN splitted to columns by key */
	CF_ISTORE_FETCHVAL,		/* -> operation on istore */
	CF_ISTORE_SEED,		/* istore_seed */
	CF_ISTORE_ACCUMULATE,	/* accumulate */
	CF_AJTIME_OPERATOR,	/* ajtime operation */
	CF_AJTIME_TO_TIMESTAMP,	/* ajtime to timestamp */
	CF_DATE_TRUNC,		/* date_trunc function */
	CF_TIMESTAMPTZ_PL_INTERVAL,	/* timestamptz + interval */
	CF_AJTIME_PL_INTERVAL,
	CF_AJTIME_MI_INTERVAL,
	CF_AJTIME_TYPE,		/* ajtime type */
	CF_AJTIME_DAY_DIFF
} custom_object_type;

typedef struct CustomObjectDef
{
	Oid						cf_oid;
	custom_object_type		cf_type;
	char					custom_name[NAMEDATALEN];	/* \0 - no custom name, \1 - many names */
	void				   *context;
} CustomObjectDef;

typedef struct CustomColumnInfo
{
	Oid		relid;
	int		varattno;
	char	colname[NAMEDATALEN];
	custom_object_type coltype;

	CHRemoteTableEngine	table_engine;
	char	signfield[NAMEDATALEN];
} CustomColumnInfo;

extern CustomObjectDef *checkForCustomFunction(Oid funcid);
extern CustomObjectDef *checkForCustomType(Oid typeoid);
extern void modifyCustomVar(CustomObjectDef *def, Node *node);
extern void ApplyCustomTableOptions(CHFdwRelationInfo *fpinfo, Oid relid);
extern CustomColumnInfo *GetCustomColumnInfo(Oid relid, uint16 varattno);
extern CustomObjectDef *checkForCustomOperator(Oid opoid, Form_pg_operator form);

#endif							/* POSTGRES_FDW_H */
