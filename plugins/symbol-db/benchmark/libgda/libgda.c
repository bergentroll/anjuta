#include <libgda/libgda.h>
#include <gtk/gtk.h>
#include <sql-parser/gda-sql-parser.h>

GdaConnection *open_connection (void);
void display_products_contents (GdaConnection *cnc);
void create_table (GdaConnection *cnc);
void insert_data (GdaConnection *cnc);
void update_data (GdaConnection *cnc);
void delete_data (GdaConnection *cnc);
const GdaStatement *get_insert_statement_by_query_id (GdaSet **plist);
const GdaStatement *get_select_statement_by_query_id (GdaConnection * cnc, GdaSet **plist);

void run_sql_non_select (GdaConnection *cnc, const gchar *sql);

GdaSqlParser *sql_parser;
GQueue *mem_pool_string;
GQueue *values_queue;

#define MEMORY_POOL_STRING_SIZE			100000
#define DUMMY_VOID_STRING				""
#define MP_VOID_STRING					"-"

#define MP_RESET_OBJ_STR(gvalue) \
		g_value_set_static_string (gvalue, DUMMY_VOID_STRING);

#define MP_LEND_OBJ_STR(OUT_gvalue) \
		OUT_gvalue = (GValue*)g_queue_pop_head(mem_pool_string); \
		MP_RESET_OBJ_STR(OUT_gvalue);

#define MP_RETURN_OBJ_STR(gvalue) \
		g_value_set_static_string (gvalue, MP_VOID_STRING); \
		g_queue_push_head(mem_pool_string, gvalue); 

#define MP_SET_HOLDER_BATCH_STR(param, string_, ret_bool, ret_value) { \
	GValue *value_str; \
	MP_LEND_OBJ_STR(value_str); \
	g_value_set_static_string (value_str, string_); \
	ret_value = gda_holder_take_static_value (param, value_str, &ret_bool, NULL); \
	if (ret_value != NULL && G_VALUE_HOLDS_STRING (ret_value) == TRUE) \
	{ \
		MP_RETURN_OBJ_STR(ret_value); \
	} \
}

static GdaDataModel *
execute_select_sql (GdaConnection *cnc, const gchar *sql)
{
	GdaStatement *stmt;
	GdaDataModel *res;
	const gchar *remain;
	GError *err = NULL;
	
	stmt = gda_sql_parser_parse_string (sql_parser, sql, &remain, NULL);	

	if (stmt == NULL)
		return NULL;
	
	res = gda_connection_statement_execute_select (cnc, 
												   (GdaStatement*)stmt, NULL, &err);
	if (!res) 
		g_message ("Could not execute query: %s\n", sql);

	if (err)
	{
		g_message ("error: %s", err->message);
	}

	g_object_unref (stmt);
	
	return res;
}


const GdaStatement *
get_insert_statement_by_query_id (GdaSet **plist)
{
	GdaStatement *stmt;
	gchar *sql = "INSERT INTO sym_type (type_type, type_name) VALUES (## /* name:'type' "
 				"type:gchararray */, ## /* name:'typename' type:gchararray */)";

	/* create a new GdaStatement */
	stmt = gda_sql_parser_parse_string (sql_parser, sql, NULL, 
										 NULL);

	if (gda_statement_get_parameters ((GdaStatement*)stmt, 
									  plist, NULL) == FALSE)
	{
		g_warning ("Error on getting parameters");
	}

	return stmt;
}

const GdaStatement *
get_select_statement_by_query_id (GdaConnection * cnc, GdaSet **plist)
{
	GdaStatement *stmt;
	gchar *sql = "SELECT count(*) FROM SYM_TYPE";
	
	GdaSqlParser *parser = gda_connection_create_parser (cnc);
	
	/* create a new GdaStatement */
	stmt = gda_sql_parser_parse_string (parser, sql, NULL, 
										 NULL);

	if (gda_statement_get_parameters ((GdaStatement*)stmt, 
									  plist, NULL) == FALSE)
	{
		g_warning ("Error on getting parameters");
	}

	return stmt;	
}

static void 
load_queue_values ()
{
	gchar line[80];
	values_queue = g_queue_new ();

	FILE *file = fopen ("./hash_values.log", "r");

	while( fgets(line,sizeof(line),file) )
	{
		/*g_message ("got %s", line);*/
		g_queue_push_tail (values_queue, g_strdup (line));
	}	
	
	fclose (file);
}


static void
create_memory_pool ()
{
	gint i;
	mem_pool_string = g_queue_new ();

	g_message ("creating memory pool...");
	for (i = 0; i < MEMORY_POOL_STRING_SIZE; i++) 
	{
		GValue *value = gda_value_new (G_TYPE_STRING);
		g_value_set_static_string (value, MP_VOID_STRING);		
		g_queue_push_head (mem_pool_string, value);
	}	
	g_message ("..OK");
}

static void 
clear_memory_pool ()
{
	g_queue_foreach (mem_pool_string, (GFunc)gda_value_free, NULL);
	g_queue_free (mem_pool_string);
	
	mem_pool_string = NULL;
}

int
main (int argc, char *argv[])
{
	if ( !g_thread_supported() )
		g_thread_init( NULL );

	g_type_init();
	gtk_init(&argc, &argv);
	
	gda_init ();
    GdaConnection *cnc;

	/* open connections */
	cnc = open_connection ();
	create_table (cnc);
	
	
	/* load        $ wc -l hash_values.log 
	 * 20959 hash_values.log
	 * into our queue.
	 */
	load_queue_values ();

	create_memory_pool ();

	insert_data (cnc);	

	
    gda_connection_close (cnc);
	g_object_unref (cnc);

	clear_memory_pool ();
	
    return 0;
}

/*
 * Open a connection to the example.db file
 */
GdaConnection *
open_connection ()
{
        GdaConnection *cnc;
        GError *error = NULL;

	/* open connection */
        cnc = gda_connection_open_from_string ("SQLite", "DB_DIR=.;DB_NAME=example_db", NULL,
					       GDA_CONNECTION_OPTIONS_THREAD_SAFE,
					       &error);
        if (!cnc) {
                g_print ("Could not open connection to SQLite database in example_db.db file: %s\n",
                         error && error->message ? error->message : "No detail");
                exit (1);
        }

	/* create an SQL parser */
	sql_parser = gda_connection_create_parser (cnc);
	if (!sql_parser) /* @cnc doe snot provide its own parser => use default one */
		sql_parser = gda_sql_parser_new ();
	/* attach the parser object to the connection */
	g_object_set_data_full (G_OBJECT (cnc), "parser", sql_parser, g_object_unref);

        return cnc;
}

/*
 * Create a "products" table
 */
void
create_table (GdaConnection *cnc)
{
	run_sql_non_select (cnc, "DROP table IF EXISTS sym_type");
        run_sql_non_select (cnc, "CREATE TABLE sym_type (type_id integer PRIMARY KEY AUTOINCREMENT,"
                   "type_type text not null,"
                   "type_name text not null,"
                   "unique (type_type, type_name))");
}

/*
 * Insert some data
 *
 * Even though it is possible to use SQL text which includes the values to insert into the
 * table, it's better to use variables (place holders), or as is done here, convenience functions
 * to avoid SQL injection problems.
 */
void
insert_data (GdaConnection *cnc)
{	
	GdaSet *plist = NULL;
	const GdaStatement *stmt;
	GdaHolder *param;
	GValue *ret_value;
	gboolean ret_bool;
	gint i;
	gdouble elapsed_DEBUG;
	GTimer *sym_timer_DEBUG  = g_timer_new ();	

	g_message ("begin transaction...");
	gda_connection_begin_transaction (cnc, "symtype", 
	    GDA_TRANSACTION_ISOLATION_READ_UNCOMMITTED, NULL);
	g_message ("..OK");

	gint queue_length = g_queue_get_length (values_queue);

	g_message ("populating transaction..");
	for (i = 0; i < queue_length; i++)
	{
		gchar * value = g_queue_pop_head (values_queue);	
		gchar **tokens = g_strsplit (value, "|", 2);
		GdaSet *last_inserted = NULL;
		
		if ((stmt = get_insert_statement_by_query_id (&plist))
			== NULL)
		{
			g_warning ("query is null");
			return;
		}
		
		/* type parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "type")) == NULL)
		{
			g_warning ("param type is NULL from pquery!");
			return;
		}

		MP_SET_HOLDER_BATCH_STR(param, tokens[0], ret_bool, ret_value);

		/* type_name parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "typename")) == NULL)
		{
			g_warning ("param typename is NULL from pquery!");
			return;
		}
		
		MP_SET_HOLDER_BATCH_STR(param, tokens[1], ret_bool, ret_value);

		/* execute the query with parametes just set */
		gda_connection_statement_execute_non_select (cnc, 
														 (GdaStatement*)stmt, 
														 (GdaSet*)plist, &last_inserted,
														 NULL);

		g_strfreev(tokens);
		/* no need to free value, it'll be freed when associated value 
		 * on hashtable'll be freed
		 */

		if (last_inserted)
			g_object_unref (last_inserted);
	}
	
	elapsed_DEBUG = g_timer_elapsed (sym_timer_DEBUG, NULL);
	g_message ("..OK (elapsed %f)", elapsed_DEBUG);

	
	g_message ("committing...");
	gda_connection_commit_transaction (cnc, "symtype", NULL);

	elapsed_DEBUG = g_timer_elapsed (sym_timer_DEBUG, NULL);
	g_message ("..OK (elapsed %f)", elapsed_DEBUG);	
}



/*
 * run a non SELECT command and stops if an error occurs
 */
void
run_sql_non_select (GdaConnection *cnc, const gchar *sql)
{
    GdaStatement *stmt;
    GError *error = NULL;
    gint nrows;
	const gchar *remain;
	GdaSqlParser *parser;

	parser = g_object_get_data (G_OBJECT (cnc), "parser");
	stmt = gda_sql_parser_parse_string (parser, sql, &remain, &error);
	if (remain) 
		g_print ("REMAINS: %s\n", remain);

        nrows = gda_connection_statement_execute_non_select (cnc, stmt, NULL, NULL, &error);
        if (nrows == -1)
                g_error ("NON SELECT error: %s\n", error && error->message ? error->message : "no detail");
	g_object_unref (stmt);
}