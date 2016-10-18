#include <ctype.h>

#include <wh/acedb.h>

#include <wac/ac.h>
#include <wac/ac_.h>
#include <wac/acclient_.h>

static char* ac_sys (AC_DB db) ;

BITSET_MAKE_BITFIELD 	 /*	 define bitField for bitset.h ops */

static void ac_fillobj (AC_OBJ obj) ;
static void cache_detach (struct ac_real_object *obj) ;
static AC_OBJ cache_find ( AC_DB db, int class_number , char *name, AC_HANDLE handle) ;
static AC_OBJ cache_create (AC_DB db, int class_number, char *name , AC_HANDLE handle) ;
static int active_objects_count (unsigned char *buff) ;

static void ac_dump_object (char *s, AC_OBJ obj, struct ac_real_object *robj)
{
  if (robj && strcmp (robj->name, "a") != 0)
    return ;
  if (0)  ac_dump_object (0, 0, 0) ; /* for compiler happiness */
  printf ("%-20s ", s) ;
  if (obj)
    printf ("obj %08x ", (int)obj) ;
  else
    printf ("             ") ;
  if (robj)
    {
      printf ("x %08x ref %d ", (int)robj, robj->refcount) ;
      printf ("t %08x class %02x name %s", (int)robj->table, robj->class_number, robj->name) ;
    }
  printf ("\n") ;
}

/********************************************************************/

/*
 * Here we see some interesting implications of the handle library.
 * You call ac_free () to free the object, but it doesn't appear
 * to do much of anything.  But when ac_free () calls messfree (), 
 * messfree () calls the finalization function for that data item.
 * The finalization function was registered when you created the
 * data element with halloc ().
 */


static void ac_free_real_object (struct ac_real_object *robj )
{
  cache_detach (robj) ;
  ac_free (robj->table) ;
  messfree (robj->a_data) ;
  bitSetDestroy (robj->hasTag) ;
  arrayDestroy (robj->tagLine) ;
  /*
   * the tag_name fields in tagLine are all pointers
   * into AC_DB object data and do not need to be freed
   * by us.
   */
  /* robj->class is pointer into AC_DB object data */
  /* robj->name is pointer into AC_DB */

  robj->db = 0 ;

  messfree (robj) ;
}

static void ac_finalize_object (void *v)
{
  AC_OBJ obj = (AC_OBJ)v ;
  struct ac_real_object *x ;
  BOOL test = FALSE ;

  x = obj->x ;

  if (x == 0)
    return ; /* mieg */
  if (x == 0)
    messcrash ("finalizing AC_OBJ with NULL x pointer") ;

  test = !strcmp (ac_name(obj), "self_ref") ;
  obj->x = 0 ;

  if (x->refcount > 0)
    x->refcount-- ;
  if (test)
    printf ("ac_finalize self_ref recfount now =%d\n",x->refcount ) ;
  if (x->refcount > 0)
    return ;

  if (test)
    printf ("ac_finalize freeing self_ref\n") ;
  ac_free_real_object (x) ;
}

static void ac_finalize_keyset (void *v)
{
  AC_KEYSET ks = (AC_KEYSET)v ;
  char buf[100] ;

  ks->magic = 0 ;
  ks->x->refcount-- ;
  if (ks->x->refcount > 0)
    return ;

  sprintf (buf, "kclear _aks%d",  ks->x->id) ;
  ks->x->db->lazy_command (ks->x->db, buf) ;
  messfree (ks->x) ;
}


static void ac_finalize_iterator (void *v)
{
  struct ac_iter * iter = (struct ac_iter*)v ;
  int x ;

  iter->magic = 0 ;

  iter->real_keyset->refcount-- ;
  if (iter->real_keyset->refcount <= 0)
    messfree (iter->real_keyset) ;

  /*
   * must free any un-returned objects we have in the iterator
   */
  for (x = iter->ra_next ; x < iter->ra_max ; x++)
    ac_free (iter->obj[x]) ;

}

static void ac_finalize_db (void *v)
{
  int x, y, num ;
  struct ac_db *adb = (struct ac_db *) v ;
  Array real_objects[ 256] ;

  memset (real_objects, 0, 256 * sizeof (Array)) ;
  adb->magic = 0 ;
  /*
  * close the transport that talks to the database
  */
  if (adb->close_transport)
    (adb->close_transport) (adb) ;

  num = 0 ;

  /*
   * Empty the object cache.  For each class, we want to find
   * all the objects in the cache and destroy them.
   */
  for (x=0 ; x<256 ; x++)
    {
      void *key, *value ;
      if (! adb->cached_objects[x])
	continue ;

      /*
       * First:  walk through the cache and make a list of
       * all the ac_real_object structs.  You can't use
       * the associator iterator because we will be changing
       * the associator at various points in the loop.
       *
       * We increase the refcount on every object that we
       * are about to free, so we can be sure that it does
       * not get freed too early.
       */

      real_objects[x]=arrayCreate (100, struct ac_real_object * ) ;
      num = 0 ;

      key = 0 ;
      value = 0 ;
      while (uAssNext (adb->cached_objects[x], &key, &value))
	{
	  struct ac_real_object *t ;
	  t = (struct ac_real_object *)value ;
	  if (! t)
	    continue ;
	  t->refcount++ ;
	  if (t->name && !strcmp (t->name, "self_ref"))
	    printf ("db_finalize increased refcount of self_ref, now %d\n",  t->refcount) ;
	  array (real_objects[x], num++, struct ac_real_object *) = t ;
	}
    }

  for (x=0 ; x<256 ; x++)
    {
      if (!real_objects[x])
	continue ;
      /*
       * Next: free all the data tables from the cached objects.  This
       * may try to deallocate some AC_OBJ objects, but we incremented
       * all the ref counts above so nothing is really deallocated.
       */
      for (y=0 ; y < arrayMax (real_objects[x]) ; y++)
	{
	  struct ac_real_object *t ;
    	  AC_TABLE tb ;
	  t = arr (real_objects[x], y, struct ac_real_object *) ;
	  if (t && t->table)
	    {
	      /*
	       * tables point back into objects, so when you free the
	       * table, you need to make sure you can't find your way
	       * back to this object and try to free the table again.
	       */ 
	      if (t->name && !strcmp (t->name, "self_ref"))
		printf ("db_finalize fres the table of self_ref, refcount=%d\n",  t->refcount) ;
	      tb =  t->table ;
	      t->table = 0 ;
	      ac_free (tb) ;
	    }
	}
    }

  for (x=0 ; x<256 ; x++)
    {
      if (!real_objects[x])
	continue ;
      /*
       * Now we believe there are no more AC_OBJ that refer to any of
       * our struct ac_real_object -- at least none that are under our
       * control.  We can free our ac_real_object now.
       */
      for (y = 0 ;  y < arrayMax (real_objects[x]) ; y++)
	{
	  struct ac_real_object *t ;
	  t = arr (real_objects[x], y, struct ac_real_object *) ;
	  if (t)
	    {
	      t->refcount-- ;	/* undo the increment we made above */
	      if (t->name && !strcmp (t->name, "self_ref"))
		printf ("db_finalize decreased refcount of self_ref, now %d\n",  t->refcount) ;
	      ac_free_real_object (t) ;
	    }
	}

      arrayDestroy (real_objects[x]) ;

    }
  /*
   * now destroy the object cache itself - the symbol table is made
   * of a dict and an associator for each class. 
   */
  for (x=0 ; x<256 ; x++)
    {
      if (adb->cached_names[x])
	uDictDestroy (adb->cached_names[x]) ;
      if (adb->cached_objects[x])
	uAssDestroy (adb->cached_objects[x]) ;
      if (adb->flags[x])
        arrayDestroy (adb->flags[x]) ;
    }

  dictDestroy (adb->classDict) ;
  dictDestroy (adb->tag_dict) ;
}

/********************************************************************/

/*
 * Contact with the server is through 
 *  void ac_partial_command_XXX (
 *
 *	AC_DB db, 			db to contact
 *
 *	char *command, 			command to send or "encore" 
 *
 * 	unsigned char **response, 	filled in with ptr to
 *				   	response data
 *
 *	int *response_length, 		filled in with len of output 
 *					data
 *
 *	unsigned char **response_free, 	free this address when done 
 *					with the response - may not
 *					be the same as the response
 *
 *	int *encore			filled in with 0 if you got
 *					the entire response or 1 if
 *					there is more
 *	)
 *
 * The server may choose to send a partial response.  If it does, *encore
 * is set and you should call ac_partial_command again with the command
 * "encore" to get the rest of the response.
 *
 * When you are done with the response, use free () to deallocate
 * the value returned in the response_free parameter.  With some 
 * transports, the response is a pointer into the middle of the data
 * block that was allocated for the response.
 *
 * The correct partial_command function is available in 
 * AC_DB->ac_partial_command
 *
 * The following includes bring in implementations of various
 * transports.  To create a new transport, include the implementation
 * code here and modify ac_open_connection () - no other changes are
 * needed.
 */

#include "../wac/acclient_rpc.c"

#include "../wac/acclient_socket.c"

#include "../wac/acclient_acetcp.c"


#define protocol_match(p,s) (strncmp (p, s, sizeof (s)-1) == 0 && ( p[sizeof (s)-1] == 0 || isdigit ((int)p[sizeof (s)-1]) ) )
	/*
	* match protocol p to constant string s - the name of the protocol matches up to the end
	* of the string or the first digit
	*/

char * ac_open_connection (AC_DB db, char *protocol, char *host, int port, char *extras )
{
  if ( protocol_match (protocol, "rpc") || protocol_match (protocol, "r"))
    return ac_open_rpc (db, protocol, host, port) ;
  if ( protocol_match (protocol, "acetcp") || protocol_match (protocol, "a"))
    return ac_open_acetcp (db, protocol, host, port) ;
  if ( protocol_match (protocol, "socket") || protocol_match (protocol, "s"))
    return ac_open_socket (db, protocol, host, port, extras) ;
  return "000 unrecognized database specifier" ;
}

/* 
   in case of success, use the returned value as a handle for
   all transcations, 
   in case of failure, we return 0 plus an error mesage
   this error message is a fixed string, so the client
   does not have to free it and the message is not volatile 
*/

AC_DB ac_open_db (char *database, char **error)
{
  char *buf, *s = 0 ;
  int port_number ;
  struct ac_db *db = 0 ;
  char *protocol, *host, *port, *extras ;

  if (error) *error = 0 ;

  if (!database || !*database)
    {
      if (error) *error = "001 bad call to ac_open, no database name provided" ;
      return 0 ;
    }

  protocol = 0 ;
  host = 0 ;
  port = 0 ;
  extras = 0 ;

  buf = strdup (database) ;
  protocol = buf ;		/* which transport we will use */
  s = strchr (protocol, ':') ;

  if (s)
	{
	*s++ = 0 ;
	host = s ;		/* which host to contact */
	s = strchr (host, ':') ;
	}
  if (s)
	{
	*s++ = 0 ;
	port = s ;		/* numeric part of address */
	s = strchr (port, ':') ;
	}
  if (s)
	{
	*s++ = 0 ;
	extras = s ;		/* further parameters, like user/pass */
	}
  if (! port )
	{
	/*
	* This used to be for compatibility with the old RPC specifier.
	* Now it chooses a default transport.
	*/
	if (host && atoi (host))
	  {
	  port = host ;
  	  host = protocol ;
	  protocol = "a" ;
	  }
	else
	  {
	  if (error) *error = "003 badly formatted database name" ;
	  return 0 ;
	  }
	}

  port_number = atoi (port) ;

  /* we allocate the db struct and initialise it */
  /* NB: halloc sets memory to zero and never fails */
  db = (AC_DB) halloc (sizeof (struct ac_db), 0) ;
  blockSetFinalise (db, ac_finalize_db) ;

  db->magic = MAGIC_BASE + MAGIC_AC_DB ;

  s = ac_open_connection (db, protocol, host, port_number, extras ) ;  
  free (buf) ;
  if (s)
    {
      messfree (db) ;
      if (error) *error = s ;
      return NULL ;
    }

  s = ac_sys (db) ;
  if (s) /* error */
    {
      messfree (db) ;
      if (error) *error = s ;
      return NULL ;
    }

  return db ;
}

/********************************************************************/
/* general acedb command, 
 *  return zero if null parameters are provided by the user.
 *      or the database reply
 *
 *  the result is allocated on the user provided handle
 *  so the user should either messfree his handle or
 *  explicitely messfree the returned buffer
 *
 * We have to copy the result because askServerBinary uses
 * malloc () but we want halloc () so we can have a handle.
 * We should fix this so we can eliminate the copy operation.
 *
 * If the server says "encore", we collect up a list of all the responses
 * and then concatenate them at the end.  This is faster that continually
 * using realloc () to make the buffer bigger, because each realloc () is
 * likely to require a copy of all the partial response data received
 * so far.
 * 
 */

unsigned char *ac_command (AC_DB db, char *command, int *result_lengthp, 
			   AC_HANDLE handle)
{
  unsigned char *response, *response_free ;
  int  response_len ;
  int  encore ;
  struct response_record
  {
    unsigned char *data ;
    unsigned char *free ;
    int len ;
  } *t ;
  Array responses ;
  int x, y ;

  /*
   * ac_partial_command sends a command to the server and gets the
   * first block of the server's reply.
   */
  (db->ac_partial_command) (db, command, &response, &response_len, &response_free, &encore) ;

  if (! encore)
    {
      /*
       * The reply contains all the reply data.  Most responses come here.
       */
      unsigned char *s ;
      s = halloc (response_len + 1, handle) ;
      memcpy (s, response, response_len) ;
      s[response_len] = 0 ;
      free (response_free) ;
      if (result_lengthp)
	*result_lengthp = response_len ;
      return s ;
    }

  /*
   * The server told us that we will get more data if we ask for "encore".
   * Keep doing that until we get them all, then concatenate them into a
   * single buffer.
   */
  responses = arrayCreate (10, struct response_record) ;
  t = & array (responses, 0, struct response_record) ;
  t->data = response ;
  t->free = response_free ;
  t->len = response_len ;

  x = 1 ;
  while (encore)
    {
      /*
       * getting the next section of the response
       */
      (db->ac_partial_command) (db, "encore", &response, &response_len, &response_free, &encore) ;
      t = & array (responses, x, struct response_record) ;
      t->data = response ;
      t->free = response_free ;
      t->len = response_len ;
      x++ ;
    }

  /*
   * Count how big the returned buffer must be.
   */
  response_len = 0 ;
  for (x=0 ; x<arrayMax (responses) ; x++)
    response_len += array (responses, x, struct response_record).len ;
 
  response = halloc (response_len + 1, handle) ;
  response[response_len] = 0 ;
 
  /*
   * Copy all the segments of the response into the buffer.
   * Free the returned segments (with free, not messfree)
   */
  y=0 ;
  for (x=0 ; x<arrayMax (responses) ; x++)
    {
      t= & array (responses, x, struct response_record) ;
      memcpy (response+y, t->data, t->len) ;
      free (t->free) ;
      y = y + t->len ;
    }

  /*
   * clean up our overhead
   */
  arrayDestroy (responses) ;
 
  /*
   * done
   */
  if (result_lengthp)
    *result_lengthp = response_len ;

  return response ;
}


/*********************************************************************
 *********************************************************************
 *********************************************************************
 **
 ** Here is a section of code that deals with parsing the -C mode
 ** server responses.  All functions and data structures are private.
 **
 ** (struct parsing *) is a cursor that the parsing functions use for
 ** their input.
 **
 ** parse_init ( char *buffer, int len, AC_DB db)
 **	create a cursor.  buffer is the text you want to parse, and
 **	len is how long it is.  db is the database we are talking
 **	to, and is required because we have to recognize names that
 **	-C encodes as numbers and because we may need to create objects
 **
 **	These functions assume that any input you give is coming from 
 **	ac_command or ac_partial_command, and therefore it is known
 **	that strlen () used in the buffer will always find a '\0' even
 **	if the buffer is not well formed.
 **
 ** parse_getc ( struct parsing *p )
 **	returns the next byte from the input stream.  returns -1 on EOF.
 ** 
 ** parse_getstring ( struct parsing *p )
 **	returns a null terminated string read from the input.  The
 **	returned pointer points into the buffer that was passed to
 **	parse_init ()
 **
 ** parse_object_header (struct parsing *in, int *class_out, char **name_out)
 **	gets the class number and object name from the beginning of an
 **	object record.  messfree () the name.
 **
 ** parse_u (struct parsing *p, void *u)
 **	gets a 4 byte quantity, performing byte swap if necessary.  the
 **	name "u" comes from the fact that there is typically a union of
 **	int, float, etc that holds the data.  the "u" parameters is the
 **	address of where to store the data.
 **
 ** parse_table (struct parsing *p, AC_TABLE table, AC_OBJ obj)
 **	parse data into an AC_TABLE.  obj is the object that the data is
 **	going into (though you must still pass the correct table) or NULL if
 **	the table is not part of an object (i.e. tablemaker or aql results).
 **	row and col are the current row/col at the start of the parsing.
 **	
 **
 ** 
 *********************************************************************/

/*
 * this is the input descriptor for the parsing functions
 */
struct parsing
{
  unsigned char * buffer ;
  int 	len ;
  int 	cursor ;
  int	swap ;
  AC_DB	db ;
} ;

/********************************************************************/

static struct parsing *
parse_init (unsigned char *buffer, int len, AC_DB db )
{
  struct parsing *p ;
  p = halloc (len, 0) ;
  p->buffer = buffer ;
  p->len = len ;
  p->cursor = 0 ;
  p->swap = db->swap_needed ;
  p->db = db ;
  return p ;
}

/********************************************************************/

/*
 * bug: inline this, but the alpha compiler doesn't know inline
 *
static int
parse_getc (struct parsing *p)
{
  int c ;
  if (p->cursor >= p->len)
    return -1 ;
  c=p->buffer[p->cursor] ;
  p->cursor++ ;
  return c ;
}
*/
/* here is an equivalent macro */
#define parse_getc(_pp) ((_pp)->cursor >= (_pp)->len ? -1 : ((_pp)->buffer[((_pp)->cursor)++]))

/********************************************************************/

static char *
parse_getstring (struct parsing *p)
{
  int len ;
  unsigned char *text ;

  len = strlen ((char *) (p->buffer+p->cursor)) ;
  text = p->buffer+p->cursor ;
  if (p->cursor+len > p->len)
    return NULL ;
  
  p->cursor += len+1 ;
  return (char *)text ;
}

/********************************************************************/

static int
parse_object_header (struct parsing *in, int *class_out, char **name_out)
{
  int c ;
  for ( ; ;)
    {
      c = parse_getc (in) ;
      if (c == -1)
	return -1 ;
      else if (c == 'N')
	{
	  *class_out = parse_getc (in) ;
	  *name_out = parse_getstring (in) ;
	  return 0 ;
	}
      else if (c == '/')
	{
	  while ((c != -1) && (c != '\n') )
	    c = parse_getc (in) ;
	}
      else if (c == '\n')
	continue ;
      else if (c == 0)
	continue ;
      else
	return -2 ;
    }
}

/********************************************************************/

static void
parse_u (struct parsing *p, void *u)
{
  unsigned char *uu = (unsigned char *)u ;
  if (p->swap)
    {
      uu[3] = parse_getc (p) ;
      uu[2] = parse_getc (p) ;
      uu[1] = parse_getc (p) ;
      uu[0] = parse_getc (p) ;
    }
  else
    {
      uu[0] = parse_getc (p) ;
      uu[1] = parse_getc (p) ;
      uu[2] = parse_getc (p) ;
      uu[3] = parse_getc (p) ;
    }
}

/********************************************************************/

static void
parse_table (struct parsing *p, AC_TABLE table, AC_OBJ obj)
{
  int kType, n ;
  char *s ;
  union
  { int	i ;
    float f ;
    KEY   k ;
    mytime_t time ;
  } u ;
  int class_number ;
  char *name ;
  int row, col ;
  int a_data_len = -1 ;
  int in_hash ;

  /*
   * -C begins it's output assuming that your cursor is at
   * the correct row, but column == -1.  The first time you
   * call parse_table, there are no rows or columns, so you start
   * at (0, -1).  On successive calls (like you might have with
   * tablemaker encores), you begin at the max row of the table, 
   * but still at column -1.
   */
  row = table->rows ;
  col = -1 ;

  in_hash = 0 ;
  for ( ; ;)
    {
      kType = parse_getc (p) ;
      switch (kType)
	{
	case -1:	/* EOF on input string */
	case '#': /* end of data */
	  return ;

 	  /*
	  * entering or leaving a #xxx construct
	  */
	case '!':
	  in_hash++ ;
	  break ;

	case '@':
	  in_hash-- ;
	  break ;

	  /* simple data */
	case 'g': /* 4 bytes deposit tag in current cell */
	  {
	  parse_u (p, &u) ;
	  s = dictName (p->db->tag_dict, u.k ) ;
	  ac_table_insert_type (table, row, col, &s, ac_type_tag) ;

	  if ( obj && ( in_hash == 0) )
	    {
	      /*
	       * if we are loading an object, we need to remember where
	       * the tag was.  Note that we do NOT just remember the
	       * first place we see the tag!  If you are in a #xxx
	       * construct, you can see duplicate tags.
	       *
	       */
	      if ( ! bit (obj->x->hasTag, u.k) )
		{
		  /*
		   * We have not seen it before.  add it, remember
		   * where it is.
		   */
		  struct tagLine nt, *t ;
		  nt.row = row ;
		  nt.col = col ;
		  nt.tag_number = u.k ;
		  nt.tag_name = s ;
		  t = & array (obj->x->tagLine, arrayMax (obj->x->tagLine), struct tagLine) ;
		  *t = nt ;
		  bitSet (obj->x->hasTag, u.k) ;
		}
	    }
          }
	  break ;

	case 'K':
	case 'k':
   	  {
	    AC_OBJ new_obj ;
	    class_number = parse_getc (p) ;
	    name = parse_getstring (p) ;
	    new_obj = cache_find ( p->db, class_number , name, NULL ) ;
	    if (! new_obj)
	      new_obj = cache_create (p->db, class_number, name, NULL) ;
 	    if (kType == 'k' && ! new_obj->x->table)
	      {
		/*
		 * we know the object to be empty, so we fill it as empty
		 */
		new_obj->x->table = ac_empty_table (0, 0, 0) ;
		new_obj->x->filled = TRUE ;
		new_obj->x->get_date = ++(p->db->date) ;
	      }

	    ac_table_insert_type (table, row, col, &new_obj, ac_type_obj ) ;
 	    messfree (new_obj) ;
  	  }
	  break ;

	case 'i': /* 4 bytes deposit integer in current cell */
	  parse_u (p, &u) ;
	  ac_table_insert_type (table, row, col, &u, ac_type_int) ;
	  break ;

	case 'f': /* 4 bytes deposit float in current cell */
	  parse_u (p, &u) ;
	  ac_table_insert_type (table, row, col, &u, ac_type_float) ;
	  break ;

	case 'd': /* 4 bytes deposit date in current cell */
	  parse_u (p, &u) ;
	  ac_table_insert_type (table, row, col, &u, ac_type_date) ;
	  break ;

	case 'v': /* void : vNULL*/
	  /*
	   * We must insert a cell that is ac_type_empty even though
	   * that is the value already there.  By performing the
	   * insert, we also increase the size of the table.
	   */
	  ac_table_insert_type (table, row, col, &u, ac_type_empty) ;
	  break ;

	case 't': /* string "\0"	deposit text in current cell */
	  s = parse_getstring (p) ;
	  ac_table_insert_text (table, row, col, s) ;
	  break ;

	  /* geometry of the table */
	case '>': 
	  col++ ; 
	  break ;

	case '.': /* increment row */
	  row++ ;
	  ac_table_copydown (table, row, col) ;
	  break ;

	case '<': /* increment row, decrement col */
	  row++ ;
	  col-- ;
	  ac_table_copydown (table, row, col) ;
	  break ;

	case 'l': /* left: subtract %c from column.  Do not change row
		   * this is always followed by a "." that increments the row 
		   */
	  n = parse_getc (p) ;
	  col -= n ;
	  break ;

	  /* null operations */

	case '\n': /* no operation */
	  break ;

	case '/': /* acedb comment */
	  while (kType != '\n')
	    kType = parse_getc (p) ;
	  break ;

	case 'c':	
	  /*
	   * c integer
	   * tells the length of the following A data
	   */
	  parse_u (p, &u) ;
	  a_data_len = u.i ;
	  break ;

	case 'D':	/* dna */
	case 'p':	/* peptide */
	  {
	  /*
 	   * dna and peptide have the same format - a_data_len bytes of
	   * unformatted data.
 	   */

	  if (! obj)
	    messcrash ("server broken: trying to load type A data into non-object table") ;

	  if (a_data_len == -1)
	    messcrash ("server broken: sent type A data without length") ;

	  /*
	   * get some memory, copy the data in.
	   */
	  if (obj->x->a_data)
	    messfree (obj->x->a_data) ;

	  obj->x->a_data_len = a_data_len ;

	  obj->x->a_data = halloc (a_data_len+1, 0) ;
	  s = (char *) obj->x->a_data ;

 	  while (a_data_len > 0)
	    {
	      *s++ = parse_getc (p) ;
	      a_data_len -- ;
	    }
 	  *s++ = 0 ;

	  /*
	   * always have an empty table - it makes the code easier to
	   * read if we don't have to litter it with " if (x->table) ... "
	   */
  	  if (! obj->x->table)
	    obj->x->table = ac_empty_table (0, 0, 0) ;

	  /*
	   * object is now filled.  also, clear a_data_len in case the
	   * server sends another type A data block.
	   */
	  obj->x->filled = TRUE ;
	  obj->x->get_date = ++(p->db->date) ;
	  a_data_len = -1 ;

	  }
          break ;

	case 0:
	  /*
	  *  buggy servers sometimes insert a 00 byte
	  */
	  break ;

	default:
	  printf ("in parse table: %d of %d bytes\n", p->cursor, p->len) ;
	  printf ("non implemented type in wac parse_table: %d %c", kType, kType) ;
	  messcrash ("non implemented type in wac parse_table: %d %c", kType, kType) ;

	  break ;
	}
    }
}

/********************************************************************
*********************************************************************
*********************************************************************
**
** Here is a section of code that deals with the object cache.  It
** is not very elaborate, but it gets us most of the speedup of a
** cache by eliminating a server transaction each time a cached 
** object is found.
**
** The cache uses a dict and an associator to make a symbol table.
** To find an object, you look it up in the dict, then use the
** index as the key to the associator.  The resulting pointer is
** the AC_OBJ.
**
** The cache currently has no provision for keeping objects that
** have been freed recently.  As long as we continue to use this
** library primarily in CGI programs, that is probably not going
** to be a problem.
**
*********************************************************************/

void ac_db_refresh (AC_DB db)
{
  db->refresh_date = ++db->date ; /* all previously cached object are now too old */
}

/*
 * cache_find () locates the object in the cache, increments the reference
 * count, and returns it.  You must ac_free () the object after you get
 * it from cache_find ().
 */

static AC_OBJ
cache_find ( AC_DB db, int class_number, char *name, AC_HANDLE handle)
{
  struct ac_real_object *obj ;
  AC_OBJ ersatz ;
  int index ;
  void *tmp ;

  /*
   * no cached names in this class
   */
  if (! db->cached_names[class_number])
    return NULL ;

  if (! db->cached_objects[class_number] )
    return NULL ;

  /*
   * there are cached names, but not this one
   */

  if ( ! dictFind (db->cached_names[class_number], name, &index) )
    return NULL ;

  if ( ! assFind ( db->cached_objects[class_number], assVoid (index), &tmp) )
    return NULL ;

  /*
   * found it
   */
  obj = (struct ac_real_object *) tmp ;

  if (obj->get_date < db->refresh_date) /* cached object is too old */
    {
      cache_detach (obj) ;
      return NULL ;
    }

  obj->refcount++ ;

  ersatz = halloc (sizeof (struct ac_object), handle) ;
  blockSetFinalise ( ersatz, ac_finalize_object ) ;
  ersatz->magic = MAGIC_BASE + MAGIC_AC_OBJECT ;
  ersatz->x = obj ;
  if (!strcmp (ac_name(ersatz), "self_ref"))
    printf ("cache_find self_ref, refcount=%d\n",  obj->refcount) ;
  return ersatz ;
}

/*
 * cache_create () makes an empty object in the cache.  You must ac_free () 
 * the object when you are done with it.
 *
 * The name parameter is not needed after cache_create returns.
 */

static AC_OBJ
cache_create (AC_DB db, int class_number, char *name, AC_HANDLE handle )
{
  struct ac_real_object *x ;
  AC_OBJ ersatz ;
  int index ;

  /**
   ** create the object
   **/

  x = (struct ac_real_object *) halloc (sizeof (struct ac_real_object), 0) ;

  x->refcount = 1 ;
  x->db = db ;
  x->table = NULL ;
  /*
   * An empty object would have a table with 0 rows
   */
  x->a_data = NULL ;
  x->filled = FALSE ;
  x->hasTag = bitSetCreate (db->maxTag, 0) ;
  if (db->maxTag > 3000)
    abort () ;
  x->tagLine = arrayHandleCreate (8, struct tagLine, 0) ;
  x->class = dictName (db->classDict, class_number) ;
  x->class_number = class_number ;

  /**
   ** add the object to the cache
   **
   **/

  if (! db->cached_names[class_number])
    {
      /*
       * No dict present - create it.  Index 0 is not allowed in
       * associators, so we have to use it up by inserting an invalid
       * object name.
       * Do not allocate it on the db->handle
       */
      db->cached_names[class_number] = dictHandleCreate (100, 0) ;
      dictAdd ( db->cached_names[class_number], "_____", &index ) ;
    }

  dictAdd ( db->cached_names[class_number], name, &index) ;
  x->name = dictName ( db->cached_names[class_number], index) ;

  /*
   * The associator part of the symbol table.
   * Do not allocate it on the db->handle
   */
  if (! db->cached_objects[class_number] )
    db->cached_objects[class_number] = assHandleCreate (0) ;
  assInsert ( db->cached_objects[class_number], assVoid (index), (void *)x) ;

  ersatz = halloc (sizeof (struct ac_object), handle) ;
  blockSetFinalise ( ersatz, ac_finalize_object) ;
  ersatz->magic = MAGIC_BASE + MAGIC_AC_OBJECT ;
  ersatz->x = x ;
 
  if (!strcmp (ac_name(ersatz), "self_ref"))
    printf ("cache_create self_ref, refcount=%d\n",  ersatz->x->refcount) ;
  return ersatz ;
} /* cache_create */

/*
 * cache_detach () just takes it out of the cache.  It does not destroy
 * the object.
 */
static void
cache_detach (struct ac_real_object *obj)
{
  AC_DB db ;
  int index ;
  int class_number ;

  db = obj->db ;

  class_number = obj->class_number ;

  if ( ! dictFind (db->cached_names[class_number], obj->name, &index) )
    abort () ;	/* never happens */

  assRemove (db->cached_objects[class_number], assVoid (index)) ;
}

/********************************************************************/
/********************************************************************/
/********************************************************************/
/********************************************************************/
/********************************************************************/

/*********************************************************************
 *********************************************************************
 **
 ** assorted private functions 
 **
 *********************************************************************
 ********************************************************************/

/*
 * find_one () - find one object in the database and leave it in the
 * active keyset.  This is used by some of our internal functions, but
 * can never be public because the AC library does not have a concept
 * of the active keyset.
 */

static int find_one (AC_DB db, char *class, char *name)
{
  char *new_class ;
  char *new_name ;
  char *command ;
  unsigned char *response ;
  int nn ;
  
  new_class = freeprotect (class) ;
  new_class = strnew (new_class, 0) ;

  new_name = freeprotect (name) ;
  new_name = strnew (new_name, 0) ;

  command = messalloc (30 + strlen (new_class) + strlen (new_name)) ;
  if (0) sprintf (command, "query find %s IS %s\nlist -C", new_class, new_name ) ;
  else sprintf (command, "query find %s IS %s", new_class, new_name ) ;

  response = ac_command (db, command, 0, 0) ;

  nn = active_objects_count (response) ;

  messfree (response) ;
  messfree (command) ;
  messfree (new_name) ;
  messfree (new_class) ;


  return nn ;
}


/*
 * ac_fillobj () - fetch the data for an unfilled object
 *
 * ac_fillobj_part2 () - entry point for filling object when it is the
 * 	only one in the active keyset
 */
static void ac_fillobj_part2 (AC_DB db, AC_OBJ *objp, BOOL create, int class_number, char *name, AC_HANDLE handle) ;

static void 
ac_fillobj (AC_OBJ obj)
{
  int class_number ;
  if (obj->x->filled)
    return ;

  /*
   * find the object in the database server
   */
  if (find_one (obj->x->db, obj->x->class, obj->x->name) == 0)
    return ;

  /*
   * Turn the class name into a class number.  If we do not know
   * the class name, obviously we will not know the object.
   */
  if (!dictFind (obj->x->db->classDict, obj->x->class, &class_number))
    return  ;

  ac_fillobj_part2 (obj->x->db, &obj, FALSE, class_number, obj->x->name, 0) ;
}

/*
 * ac_fillobj_part2 () assumes that the object is currently the only object
 * in the active keyset.  That is OK because either ac_fillobj () or ac_get ()
 * caused that to be true before calling here.
 *
 * This entry point only exists so we can eliminate the server transaction
 * in ac_fillobj () when you ac_get () an object.
 */
static void
ac_fillobj_part2 (AC_DB db, AC_OBJ *objp, BOOL create, int class_number, char *name, AC_HANDLE handle)
{
  unsigned char *response ;
  int new_class ;
  char *new_name ;
  int response_len ;
  struct parsing *p = 0 ;
  AC_TABLE table ;
  /*
   * Make a table for the object data.  The object is now empty instead
   * of unfilled.  We just need to fill in the data.  If the server
   * response is that there is no data, we do not need to do anything
   * further.
   */
  table = ac_empty_table (10, 0, 0) ;
  /*
   * Show the object.  If the response does not contain any data, then
   * the object is empty.  We already have an empty table, so the object
   * is already empty (as opposed to un-filled) .
   */
  response = ac_command (db, "show -C", &response_len, 0) ;
  if (response)
    {
      p = parse_init (response, response_len, db) ;
      /*
       * must consume the object header even though it contains nothing interesting
       */
      if (parse_object_header (p, &new_class, &new_name ) == 0)
	{
	  if (class_number != new_class)
	    messcrash ("ac_fillobj - server responded with wrong class") ;
	  if (strcasecmp (name, new_name) != 0)
	    messcrash ("ac_fillobj - server responded with wrong object") ;

	  if (create)
	    *objp = cache_create (db, class_number, new_name, handle) ;
	  (*objp)->x->table = table ; 
	  (*objp)->x->filled = TRUE ;
	  (*objp)->x->get_date = ++(db->date) ;
	  /*
	   * parse the object data:
	   */
	  parse_table (p, table, *objp) ;
	}
    }
  else
    {
      if (create)
	{
	  *objp = cache_create (db, class_number, name, handle) ;
	}
      (*objp)->x->table = table ; 
      (*objp)->x->filled = TRUE ;
      (*objp)->x->get_date = ++(db->date) ;
    }

  messfree (response) ;
  messfree (p) ;
  return ;
} /* ac_fillobj_part2 */

/********************************************************************/

/* Private: called by ac_tag_type
 *   Locate the tag in the object table
 */
static BOOL ac_findTag (AC_OBJ obj, char *tag, int *rowp, int *colp)
{
  int tag_number ;
  int i ;
  Array lines ;
  struct tagLine *t ;

  if (! obj->x->filled)
    ac_fillobj (obj) ;

  if (! dictFind (obj->x->db->tag_dict, tag, &tag_number))
    return FALSE ;

  if (! bit (obj->x->hasTag, tag_number))
    return FALSE ;

  lines = obj->x->tagLine ;
  for (i=0 ; i< arrayMax (lines) ; i++)
    {
      t = & array (lines, i, struct tagLine) ;
      if (t->tag_number == tag_number)
	{
	  *rowp = t->row ;
	  *colp = t->col ;
	  return TRUE ;
	}
    }
  return FALSE ;

} /* ac_findTag */


/****************************************************************/

/*
 * Return the number of objects stated in the response to a command
 * that alters the active keyset.
 */
static int active_objects_count (unsigned char *buff)
{
  char cc, *cp = (char *) buff, *cq, *cp0 ;
  int nn ;

  cp0 = cp ;
  while ((cq = strstr (cp, "Active Objects"))) { cp0 = cp ; cp = cq+1 ;} /* find last occurence */
  cp0 = cp ;
  while (cp0 && cp0 > (char *)buff && *cp0 != '/') cp0-- ; cp0++ ;
  while (cp0 && *cp0 == ' ') cp0++ ;
  if (sscanf (cp0, "%d%c", &nn, &cc) == 2 && cc == ' ')
    return nn ;
  return -1 ;      
}

/****************************************************************/

/*
 * load a named keyset into the active keyset
 */

void ac_kget (struct ac_real_keyset *k)
{
  char buff[30] ;
  
  sprintf (buff, "kget _aks%d",  k->id) ;
  k->db->lazy_command (k->db, buff) ;
}

/****************************************************************/

static char* ac_parse_sys (AC_DB db, unsigned char *binaryBuffer, int nn)
{
  int key = 0 ;
  unsigned char kType, *cp = binaryBuffer ;
  int n, ncl = 0, ntags = 0 ;
  unsigned int swap_magic ;
  char *s, buff[50] ;
  
  /*
   * the first 4 bytes are a magic cookie so we can identify what
   * sort of byte swapping is needed
   */
  memcpy (&swap_magic, cp, 4) ; cp+= 4 ; nn -= 4 ;
  switch (swap_magic)
    {
    case 0x12345678:  /* server/client use save byte rules */
      db->swap_needed = FALSE ;
      break ;
    case 0x78563412:  /* server/client disagree */
      db->swap_needed = TRUE ;
      break ;
    default:
      /*
       * No machine we support uses any other byte order than strict big endian
       * or strict little endian.  If we come here, it means there was a bug.
       */
      return "005 bad call to ac_open, server return a bad swap_magic confirmation" ;
    }

  /*
   * skip over the number of tags
   */
  cp+= 3 ;
  nn -= 3 ; 

  while (cp++, nn--)
    switch (*cp)
      {
      case '#': /* end of object */
	db->maxTag = ntags ;	/* remember the number of tags we actually saw */
	/* if (ncl != 256) abort () ; */
	return 0 ;
      case '\n': /* no operation */
      case '>': 
      case '.': /* increment row */ 
	cp++ ; nn-- ;
	kType = *cp++ ; nn-- ;
	switch (kType)
	  {
	  case 'k':
	    ncl++ ;
	    s = (char *)cp ;
	    if (! *s)
	      {
		sprintf (buff, "_sys_%d", ncl) ;
		s = buff ;
	      }
	    dictAdd (db->classDict, s, &key) ;
	    break ;
	  case 'g':
	    ntags++ ;
	    dictAdd (db->tag_dict, (char *)cp, &key) ;
	    break ;
	  }
	/* jump the name itself, zero terminated, followed by \n */
	n = strlen ((char *)cp) ; nn -= n + 1 ; cp += n + 1 ;
	break ;
      default:
	break ;
      }
  messcrash ("missing # terminator in ac_parse_sys") ;

  /* extract the table definition */
  return 0 ;
} /* ac_parse_sys */

/****************************************************************/
/* obtain the class and tag names from the database */
static char* ac_sys (AC_DB db)
{ 
  int nn ;
  unsigned char *binaryBuffer = 0 ;
  char *error = "004 bad call to ac_open, server did not return a system_list" ;

  db->tag_dict = dictHandleCreate (1000, 0) ;
  db->classDict = dictHandleCreate (256, 0) ;
  binaryBuffer = ac_command (db, "system_list", &nn, 0) ;
  if (binaryBuffer)
    {
      error =  ac_parse_sys (db, binaryBuffer, nn) ;
      messfree (binaryBuffer) ;
    }
  return error ;
} /* ac_sys */

/****************************************************************/

/*********************************************************************
**********************************************************************
**********************************************************************
**
** More published library entry points
**
*********************************************************************/


/********************************************************************/

char *ac_name (AC_OBJ obj)
{
  if (obj && obj->x && obj->x->name)
    return obj->x->name ;
  else
    return "(NULL)" ;
}

/********************************************************************/

char * ac_class (AC_OBJ obj)
{
  if (obj)
    return obj->x->class ;
  else
    return " (NULL)" ;
} /* ac_class */


/********************************************************************/
/* PUBLIC
 */
BOOL ac_has_tag	 (AC_OBJ obj, char *tagName)
{
  int row, col ;
  return ac_findTag (obj, tagName, &row, &col) ;
} /* ac_has_tag	 */

/********************************************************************/
/* PUBLIC 
 *  returns the type of the next cell right of the tag
 */ 
ac_type ac_tag_type (AC_OBJ obj, char *tagName)
{
  int row = 0, col = 0 ;

  if (ac_findTag (obj, tagName, &row, &col))
    return ac_table_type (obj->x->table, row, col + 1) ;
  return ac_type_empty ;
} /* ac_tag_type */


int ac_tag_int ( AC_OBJ obj, char *tag, int deflt)
{
  int row, col ;
  if (ac_findTag (obj, tag, &row, &col))
    return ac_table_int ( obj->x->table, row, col+1, deflt ) ;
  else
    return deflt ;
}
  

float ac_tag_float ( AC_OBJ obj, char *tag, float deflt)
{
  int row, col ;
  if (ac_findTag (obj, tag, &row, &col))
    return ac_table_float ( obj->x->table, row, col+1, deflt ) ;
  else
    return deflt ;
}
  

char * ac_tag_text ( AC_OBJ obj, char *tag, char * deflt)
{
  int row, col ;
  if (ac_findTag (obj, tag, &row, &col))
    return ac_table_text ( obj->x->table, row, col+1, deflt ) ;
  else
    return deflt ;
}
  

mytime_t ac_tag_date ( AC_OBJ obj, char *tag, mytime_t deflt)
{
  int row, col ;
  if (ac_findTag (obj, tag, &row, &col))
    return ac_table_date ( obj->x->table, row, col+1, deflt ) ;
  else
    return deflt ;
}
  

AC_OBJ ac_tag_obj ( AC_OBJ obj, char *tag, AC_HANDLE handle )
{
  int row, col ;
  if (ac_findTag (obj, tag, &row, &col))
    return ac_table_obj ( obj->x->table, row, col+1, handle ) ;
  else
    return NULL ;
}

char * ac_tag_printable ( AC_OBJ obj, char *tag, char *dflt )
{
  int row, col ;
  if (ac_findTag (obj, tag, &row, &col))
    return ac_table_printable ( obj->x->table, row, col+1, dflt ) ;
  else
    return dflt ;
}
  

/********************************************************************/
/* PUBLIC
 *  fetch a table from a tag in an object
 *  returns a non-writable subtable pointing into the obj->table
 */
AC_TABLE ac_tag_table (AC_OBJ obj, char *tagName, AC_HANDLE handle)
{
  int row, col ;

  if (tagName == NULL)
    {
    if (! obj->x->filled)
      ac_fillobj (obj) ;
    return ac_copy_table (obj->x->table, 0, 0, obj->x->table->rows, obj->x->table->cols, handle) ;
    }
  if (ac_findTag (obj, tagName, &row, &col))
    return ac_subtable (obj->x->table, row, col, handle) ;
  return 0 ;
} /* ac_tag_table */


/****************************************************************/
/* perform AQL query*/

AC_TABLE ac_aql_table ( AC_DB db, char *query, AC_KEYSET initial_keyset, AC_HANDLE handle)
{
  AC_TABLE tt = 0 ;
  unsigned char *binaryBuffer = 0 ;
  int nn = 0 ;

  if (initial_keyset)
    ac_kget (initial_keyset->x) ;
  if (query && *query)
    {
      char * command = halloc (strlen (query) + 100, 0) ;
      sprintf (command, "aql -C %s", query) ;
      binaryBuffer = ac_command (db, command, &nn, handle) ;
      messfree (command) ;
    }
  if (binaryBuffer)
    {
      struct parsing *p ;
      tt = ac_empty_table (100, 0, handle) ;
      p = parse_init ( binaryBuffer, nn, db) ;
      parse_table (p, tt, 0) ;
      messfree (binaryBuffer) ;
      messfree (p) ;
    }
  return tt ;
} /* ac_aql */

/****************************************************************/
/*
 * perform tablemaker query:
 *	db is the database
 * 	query is the query
 * 	where describes how to interpret the value of query
 *           ac_tablemaker_db => query is name of tablemaker query in database
 *           ac_tablemaker_file => query is a file in the client disk space
 *           ac_tablemaker_server_file => query is file name known by server
 *		Not a particularly secure feature
 *           ac_tablemaker_text => query is the actual text of a tablemaker
 *	        query that you read into your source code.
 *	parameters are parameters to substitute in the query
 */

AC_TABLE ac_tablemaker_table ( AC_DB db, char *query, AC_KEYSET initial_keyset, 
			enum ac_tablemaker_where where, 
			char *params, AC_HANDLE handle)
{
  AC_TABLE tt = 0 ;
  unsigned char *binaryBuffer = 0 ;
  int nn = 0 ;
  char *fa = "" ;

  if (initial_keyset)
    {
      if (initial_keyset->x->db != db)
	messcrash ("initial keyset does not belong to database being queried") ;
      ac_kget (initial_keyset->x) ;
      fa = "-active" ;
    }

  if (!params) params = "" ;
  if (query && *query)
    {
      char * command = halloc (strlen (query) + strlen (params) + 100, 0) ;
      switch (where)
	{
	case ac_tablemaker_db:
	  sprintf (command, "table %s -C -n  %s %s", fa, query, params) ;
	  break ;
	case ac_tablemaker_server_file:
	  sprintf (command, "table %s -C -f  %s %s", fa, query, params) ;
	  break ;
	case ac_tablemaker_file:
	  messcrash ("ac_tablemaker_file mode not supported") ;
	  break ;
	case ac_tablemaker_text:
	  sprintf (command, "table %s -C =%s %s", fa, query, params) ;
	  break ;
	}
      /*
       * it would be more efficient if we could collect the various
       * encore responses and parse each of them into the table.
       */
      if (*command)
	binaryBuffer = ac_command (db, command, &nn, handle) ;
      messfree (command) ;
    }
  if (binaryBuffer)
    {
      if (strncmp ((char *)binaryBuffer, "Sorry, ", 6) == 0)
	{
	tt = NULL ;
	}
      else
	{
        struct parsing *p ;

        p = parse_init (binaryBuffer, nn, db) ;
        tt =  ac_empty_table (20, 0, 0) ;
        parse_table (p, tt, 0) ;
        messfree (p) ;
        }

      messfree (binaryBuffer) ;
    }
  return tt ;
}

/****************************************************************/
static AC_OBJ ac_doget_obj (AC_DB db, char *classe, char *name, int fillhint, AC_HANDLE handle)
{ 
  AC_OBJ obj = 0 ;
  int class_number ;

  /*
   * Turn the class name into a class number.  If we do not know
   * the class name, obviously we will not know the object.
   */
  if (!dictFind (db->classDict, classe, &class_number))
    return NULL ;

  /*
   * See if we already have it.
   */
  obj = cache_find ( db, class_number, name, handle) ;

  if (obj)
    {
      if (fillhint && ! obj->x->filled)
        ac_fillobj (obj) ;
      return obj ;
    }


  /*
   * We do not have it.  Get it from the server.
   */

  if (find_one (db, classe, name) == 0)
    return NULL ;

  /*
   * we still need to create the object
   *
   * If we fill it, we garantee the case of the name
   * Do we need to fill it ?
   */
  if (fillhint)
    ac_fillobj_part2 (db, &obj, TRUE, class_number, name, handle ) ;
  else
    obj = cache_create (db, class_number, name, handle ) ;

  return obj ;
} /* ac_doget_obj */


/*
 * PUBLIC ac_get ()
 *
 * find an object and get it from the server
 * always fill it: this insures that casing of name is correct
 */

AC_OBJ ac_get_obj (AC_DB db, char *classe, char *name, AC_HANDLE handle)
{ return ac_doget_obj  (db, classe, name, TRUE, handle) ; }

/****************************************************************/

/*
 * PUBLIC ac_copy_obj ()
 *
 * make another copy of an object
 */

AC_OBJ ac_copy_obj (AC_OBJ obj, AC_HANDLE handle)
{
  /*
   * a lazy implementation so we can have one when we need it.
   * It would be faster to just allocate the object directly.
   */

  AC_OBJ new_obj = 0 ;
  if (obj)
    new_obj = ac_doget_obj ( obj->x->db, obj->x->class, obj->x->name, 
			 0, handle ) ;

  return new_obj ;
}


/****************************************************************/
/****************************************************************/

/*
 * make an empty keyset structure 
 *
 * This is not the same as an empty keyset.  It just initializes the
 * data structure that we use on our end.
 */
static AC_KEYSET 
empty_keyset_struct (AC_DB db, AC_HANDLE handle)
{
  struct ac_keyset *ersatz ;
  struct ac_real_keyset *ks ;

  ks = (struct ac_real_keyset *) halloc (sizeof (struct ac_real_keyset), 0) ;
  ks->db = db ;
  ks->id = ++ (db->keysetId) ; /* unique within the existence of the db handle */
  ks->max = 0 ;
  ks->refcount = 1 ;

  ersatz = (struct ac_keyset *) halloc (sizeof (struct ac_keyset), handle) ;
  blockSetFinalise (ersatz, ac_finalize_keyset) ;
  ersatz->magic = MAGIC_BASE + MAGIC_AC_KEYSET ;
  ersatz->x = ks ;

  return ersatz ;
}


/* PRIVATE
 * implements the different ..query_keyset
 */
static AC_KEYSET ac_doquery_keyset (AC_DB db, char *query, AC_KEYSET initial_keyset, AC_HANDLE handle)
{  
  unsigned char *r = 0 ;
  char *buff ;
  struct ac_keyset *ks = empty_keyset_struct (db, handle) ;

  /*
   * either load the keyset we want to start the query from, or load
   * an empty keyset so the query does not operate on whatever keyset is
   * left over from some unknown previous operation.
   */
  if (initial_keyset == (void*)1) ;
  else if (initial_keyset)
    ac_kget (initial_keyset->x) ;
  else
    {
      db->lazy_command (db, "kget _aks_no_such_keyset") ;
    }

  /*
   * now make the query and save the results in the keyset
   */
  buff = messalloc (20 + (query ? strlen (query) : 10)) ;
  if (initial_keyset == (void*)1) 
    sprintf (buff, "%s" , query) ;
  else
    sprintf (buff, "query %s" , query) ;
  db->lazy_command (db, buff) ;
  
  sprintf (buff, "kstore _aks%d" ,  ks->x->id) ;
  r = ac_command (db, buff, 0, 0) ;
  ks->x->max = active_objects_count (r) ;
  messfree (r) ;
  messfree (buff) ;
  return (AC_KEYSET) ks ;
} /* ac_query_keyset */


/* PUBLIC
 * performs a query, creates a keyset of the result
 */
AC_KEYSET ac_dbquery_keyset (AC_DB db, char *queryText, AC_HANDLE handle)
{
  return ac_doquery_keyset (db, queryText, 0, handle) ;
}
AC_KEYSET ac_ksquery_keyset (AC_KEYSET initial_keyset, char *queryText, AC_HANDLE handle)
{
  return ac_doquery_keyset (initial_keyset->x->db, queryText, initial_keyset, handle) ;
}
AC_KEYSET ac_objquery_keyset (AC_OBJ aobj, char *queryText,  AC_HANDLE handle)
{
 AC_KEYSET aks1 = ac_new_keyset (aobj->x->db, aobj, handle) , aks2 ;

  if (queryText)
    {
      aks2 = ac_doquery_keyset (aobj->x->db, queryText, aks1, handle) ;
      ac_free (aks1) ;

      return aks2 ;
    }
  else
    return aks1 ;
}

/****************************************************************/
/* PUBLIC
 * makes an empty keyset to use keyset operators on
 */
AC_KEYSET 
ac_new_keyset (AC_DB db, AC_OBJ obj, AC_HANDLE handle)
{  
  struct ac_keyset *aks ;
  char buff[50] ;

  aks = empty_keyset_struct (db, handle) ;
 
  ac_kget (aks->x) ;
  sprintf (buff, "kstore _aks%d" ,  aks->x->id) ;
  db->lazy_command (db, buff) ;
  aks->x->max = 0 ;

  if (obj)
	ac_keyset_add (aks, obj) ;

  return (AC_KEYSET) aks ;
}  /* ac_new_keyset */


/****************************************************************/
/* Private
 *   implements the boolean operation
 */
static int ac_keyset_op (AC_KEYSET aks1, AC_KEYSET aks2, char *op)
{
  unsigned char *r ;
  char buff[50] ;
  AC_HANDLE h = ac_new_handle () ;

  if (aks1->x->db != aks2->x->db)
    messcrash ("keyset operation on two keysets that are not in the same database") ;
  ac_kget (aks1->x) ;
  if (op)
    {
      aks1->x->db->lazy_command (aks1->x->db, "spush") ;
      ac_kget (aks2->x) ;
      aks1->x->db->lazy_command (aks1->x->db, op) ;
      aks1->x->db->lazy_command (aks1->x->db, "spop") ;
    }
  sprintf (buff, "kstore _aks%d" ,  aks1->x->id) ;
  r = ac_command (aks1->x->db, buff, 0, h)  ;
  aks1->x->max = active_objects_count (r) ;

  handleDestroy (h) ;
  return aks1->x->max ;
} /* ac_keyset_op */

/****************************************************************/
/* PUBLIC
 * perform boolean operation ks1 = ks1 OP ks2
 * on a keyset.
 * returns the number of element in the result
 */
int ac_keyset_and (AC_KEYSET ks1, AC_KEYSET ks2)
{
  return ac_keyset_op (ks1, ks2, "sand") ;
}

int ac_keyset_or (AC_KEYSET ks1, AC_KEYSET ks2)
{
  return ac_keyset_op (ks1, ks2, "sor") ;
}

int ac_keyset_xor (AC_KEYSET ks1, AC_KEYSET ks2)
{
  return ac_keyset_op (ks1, ks2, "sxor") ;
}

int ac_keyset_minus (AC_KEYSET ks1, AC_KEYSET ks2)
{
  return ac_keyset_op (ks1, ks2, "sminus") ;
}

/****************************************************************/
/* PUBLIC
 * makes another keyset with identical contents.  Because
 * you don't have the 3 operand instructions, you copy the
 * keyset and then use read-modify-write operations on it.
 */

AC_KEYSET ac_copy_keyset (AC_KEYSET aks, AC_HANDLE handle)
{
  AC_KEYSET aks1 = ac_new_keyset (aks->x->db, NULL, handle) ;

  ac_keyset_or (aks1, aks) ;
  return aks1 ;
} /* ac_keyset_copy */

/****************************************************************/
/* PUBLIC
 *
 * add/remove object in keyset 	
 *
 */

BOOL ac_keyset_add (AC_KEYSET aks, AC_OBJ obj)
{
  unsigned char *response ;
  char buff[50] ;

  ac_kget (aks->x) ;

  aks->x->db->lazy_command (aks->x->db, "spush") ;

  if (find_one (aks->x->db, obj->x->class, obj->x->name) == 0)
    return FALSE ;

  aks->x->db->lazy_command (aks->x->db, "sor") ;
  aks->x->db->lazy_command (aks->x->db, "spop") ;

  sprintf (buff, "kstore _aks%d" ,  aks->x->id) ;
  response = ac_command (aks->x->db, buff, 0, 0) ;
  aks->x->max = active_objects_count (response) ;
  messfree (response) ;

  return TRUE ;
} /* ac_keyset_add */

/****************************/

BOOL ac_keyset_remove (AC_KEYSET aks, AC_OBJ obj)
{
  unsigned char *response ;
  char buff[50] ;

  ac_kget (aks->x) ;

  aks->x->db->lazy_command (aks->x->db, "spush") ;

  if (find_one (aks->x->db, obj->x->class, obj->x->name) == 0)
    return FALSE ;

  aks->x->db->lazy_command (aks->x->db, "sminus") ;

  aks->x->db->lazy_command (aks->x->db, "spop") ;

  sprintf (buff, "kstore _aks%d" ,  aks->x->id) ;
  response = ac_command (aks->x->db, buff, 0, 0) ;
  aks->x->max = active_objects_count (response) ;
  messfree (response) ;

  return TRUE ;
} /* ac_keyset_remove */

/****************************************************************/
/* PUBLIC
 * returns a subset strating at x0 of length nx
 * index start at 1 and are ordered alphabetically
 * which is usefull to get slices for display
 */

AC_KEYSET ac_subset_keyset (AC_KEYSET aks, int x0, int nx, AC_HANDLE handle)
{
  unsigned char *r = 0 ;
  AC_KEYSET aks1 = ac_new_keyset (aks->x->db, NULL, handle) ;
  char buff[256] ;

  if (x0<1) x0 = 1 ;
  if (nx > aks->x->max - x0 + 1) nx = aks->x->max - x0 + 1 ;
  sprintf (buff, "subset %d %d", x0, nx) ;
  ac_kget (aks->x) ;
  aks1->x->db->lazy_command (aks1->x->db, buff) ;
  sprintf (buff, "kstore _aks%d" ,  aks1->x->id) ;
  r = ac_command (aks1->x->db, buff, 0, 0) ; 
  aks1->x->max = active_objects_count (r) ; 
  messfree (r) ;
  return aks1 ;
} /* ac_subset_keyset */

/****************************************************************/
/* PUBLIC
 * return the number of keys in a keyset
 */

int ac_keyset_count (AC_KEYSET aks)
{
  return aks->x->max ;
} /* ac_keyset_count */

/****************************************************************/

/****************************************************************
 *
 * Keyset Iterator functions
 *
 ****************************************************************/

/* PUBLIC ac_query_iter ()
 * make a query, dump resulting keyset directly into an iterator
 *	db = database to query
 * 	fillhint = true means load object data during iteration, 
 *		false means use lazy data loading
 *	initial_keyset = keyset to make active before issuing the query
 *	query = query to use
 */
AC_ITER ac_query_iter (AC_DB db, int fillhint, char *query, AC_KEYSET initial_keyset, AC_HANDLE handle)
{
  AC_ITER ret ;
  AC_KEYSET ks ;

  ks = ac_doquery_keyset (db, query, initial_keyset, 0) ;

  ret = ac_keyset_iter ( ks, fillhint, handle ) ;
 
  ac_free (ks) ;

  return ret ;
} /* ac_query_iter */

/****************************************************************/
/* PUBLIC ac_dbquery_iter ()
 * make a query, dump resulting keyset directly into an iterator
 * Short hand 1 of previous function, fillhint = true by default
 *	db = database to query
 *	query = query to use
 */
AC_ITER ac_dbquery_iter (AC_DB db, char *query, AC_HANDLE handle)
{
  return ac_query_iter (db, TRUE, query, 0, handle) ; 
} /* ac_dbquery_iter */

/****************************************************************/
/* PUBLIC ac_ksquery_iter ()
 * make a query, dump resulting keyset directly into an iterator
 * Short hand 2 of previous function, fillhint = true by default
 *	ks = keyset to query
 *	query = query to use
 */
AC_ITER ac_ksquery_iter (AC_KEYSET ks, char *query, AC_HANDLE handle)
{
  if (ks)
    return ac_query_iter (ks->x->db, TRUE, query, ks, handle) ; 
  return 0 ;
} /* ac_ksquery_iter */

/****************************************************************/
/* PUBLIC ac_objquery_iter ()
 * make a query, dump resulting keyset directly into an iterator
 * Short hand 3 of previous function, fillhint = true by default
 *	obj = single object on which we initialise the query
 *	query = query to use
 */
AC_ITER ac_objquery_iter (AC_OBJ obj, char *query, AC_HANDLE handle)
{
  AC_ITER iter = 0 ;
  if (obj)
    {
      AC_KEYSET ks = ac_objquery_keyset (obj, "IS *",  0) ;
      iter = ac_query_iter (obj->x->db, TRUE, query, ks, handle) ;
      ac_free (ks) ;
    }
  return iter ;
} /* ac_objquery_iter */

/****************************************************************/
/* PUBLIC ac_keyset_iter ()
 * create an iterator that loops over a keyset
 *	ks = the keyset to loop over
 * 	fillhint = true means load object data during iteration, 
 *		false means use lazy data loading
 *
 * Set up an iterator to loop over a keyset.  Each call to ac_next_obj
 * will return the next object from the keyset.
 *
 * This function just sets up an empty iterator.  The real work happens
 * in ac_next ()
 */
AC_ITER ac_keyset_iter (AC_KEYSET ks, int fillhint, AC_HANDLE handle) 
{ 
  struct ac_iter *iter = (struct ac_iter *) halloc (sizeof (struct ac_iter), handle) ;
  blockSetFinalise (iter, ac_finalize_iterator) ;

  /*
   * get a reference to the ac_real_keyset
   */
  iter->real_keyset = ks->x ;
  ks->x->refcount++ ;

  /*
   * generally fill in the iterator 
   */
  iter->magic = MAGIC_BASE + MAGIC_AC_ITERATOR ; 
  iter->next = 0 ;
  iter->fillhint = fillhint ;

  iter->ra_next = 0 ;
  iter->ra_max = 0 ;

  iter->user_handle = handle ;

  /*
   * these fields are copied from the keyset for convenience
   */
  iter->db = ks->x->db ;
  iter->id = ks->x->id ;
  iter->max = ks->x->max ;
 
  return (AC_ITER) iter ;
} /* ac_keyset_iter */

/****************************************************************/
/*
 * PRIVATE - ac_iter_show ()
 *
 * re-fill the iterator, using show so we get all the object data
 */

static void
ac_iter_show (AC_ITER it)
{
  unsigned char *response, *response_free ;
  int response_len ;
  struct parsing *p ;
  int class ;
  char *name, buff[80] ;
  int status ;
  int c ;

  /*
   * show the next batch of objects.  we use ac_partial_command instead
   * of ac_command.  The server may choose to send back fewer objects than
   * we ask for.  In that case, ac_command will initiate another transaction 
   * to get the rest of the response.  But we don't really need it.  We
   * continue iterating with the response we have, and when we use up the
   * objects returned, _then_ we will make another transaction anyway.
   * This reduces overhead both in transaction time and memory use in 
   * copying the multiple sections of an encore response into a single
   * buffer.
   *
   */

  sprintf (buff, "show -C -b %d -c %d", it->next, AC_ITER_OBJECT_READAHEAD) ;
  (it->db->ac_partial_command) (it->db, 
			       buff, 
			       &response, 
			       &response_len, 
			       &response_free, 
			       &c) ;

  if (! response)
    messcrash ("null server response") ;
    

  /*
   * Now we have a response containing a bunch of objects.  show sends
   * back a list of objects, which we parse as 
   *		list: element | element list ;
   *		element: object_header table ;
   */
  p = parse_init (response, response_len, it->db) ;
  while ((status = parse_object_header (p, &class, &name)) == 0)
    {
      AC_OBJ o ;
      /*
       * we may already know the object.  If not, create it.
       * Either way, we have a reference to stick on our list.
       */
      o = cache_find (it->db, class, name, it->user_handle) ;
      if (! o)
	o = cache_create (it->db, class, name, it->user_handle ) ;

      it->obj[ it->ra_max ] = o ;
      
      /*
       * If the object already has a table, that means we already
       * have filled the object.  But we have to parse the object
       * data anyway, to skip over it in the buffer.  Here, we
       * discard the data and parse it again.  Unless the data has
       * been updated in the server, the content will be invariant.
       */
      if (o->x->table)
	ac_free (o->x->table) ;

      o->x->table = ac_empty_table (10, 10, 0) ;
      o->x->filled = TRUE ;
      o->x->get_date = ++(p->db->date) ;

      parse_table (p, o->x->table, o) ;

      it->ra_max++ ;
      if (it->ra_max >= AC_ITER_OBJECT_READAHEAD)
	{
	  /*
	   * we can only come here if the server sent back more objects
	   * than we asked for.  That's ok - we can ignore them.
	   */
	  break ;
	}
    }

  messfree (p) ;

  /*
   * remember that response came from ac_partial_command, which is
   * returning a buffer that came out of the RPC library
   */
  free (response_free) ;
}

/******************************************************************/
/*
 * PRIVATE - ac_iter_list ()
 *
 * re-fill the iterator, using list so we get unfilled objects
 */

static void ac_iter_list (AC_ITER it)
{
  unsigned char *response, *response_free ;
  char buff[80] ;
  int response_len ;
  struct parsing *p ;
  int class ;
  char *name ;
  int c ;

  /*
   * fillhint is 0, so we want to just list the objects
   */

  sprintf (buff, "list -C -b %d -c %d", it->next, AC_ITER_OBJECT_READAHEAD) ;
  (it->db->ac_partial_command) (it->db, 
			       buff, 
			       &response, 
			       &response_len, 
			       &response_free, 
			       &c) ;

  if (! response)
    messcrash ("null server response") ;
    
  /*
   * Now we have a response containing a list of objects.
   */
  p = parse_init (response, response_len, it->db) ;

  /*
   * first 5 bytes are not interesting
   */
  (void) parse_getc (p) ;
  (void) parse_getc (p) ;
  (void) parse_getc (p) ;
  (void) parse_getc (p) ;
  (void) parse_getc (p) ;

  /*
   * Now parse the list of object names that came back.
   *
   * These tokens are junk:
   * 	>
   * 	.
   * 	\n
   * 	/.*\n	 (i.e. '/' to '\n')
   *
   * These are what we are looking for:
   * 	K classnumber object_name_string
   * 	k classnumber object_name_string
   */

  while ((c = parse_getc (p)) != -1)
    switch (c)
      {
      case '\n':
      case '.':
      case '>':
	break ;

      case '/':
	while ((c = parse_getc (p)) != '\n')
	  ;
	break ;

      case 'k':
      case 'K':
		
	{
	  AC_OBJ o ;

	  class = parse_getc (p) ;
	  name = parse_getstring (p) ;

	  /*
	   * we may already know the object.  If not, create it.
	   * Either way, we have a reference to stick on our list.
	   */
	  o = cache_find (it->db, class, name, it->user_handle) ;
	  if (! o)
	    o = cache_create (it->db, class, name, it->user_handle ) ;

	  if (c == 'k' && ! o->x->table )
	    {
	      /*
	       * we know the object to be empty, so we can fill the object
	       * now and avoid a server transaction to fill it later.
	       */
	      o->x->table = ac_empty_table (0, 0, 0) ;
 	      o->x->filled = TRUE ;
	      o->x->get_date = ++(it->db->date) ;
	    }

	  it->obj[ it->ra_max ] = o ;
      
	  it->ra_max++ ;
	  if (it->ra_max >= AC_ITER_OBJECT_READAHEAD)
	    {
	      /*
	       * we can only come here if the server sent back more objects
	       * than we asked for.  That's ok - we can ignore them.
	       */
	      break ;
	    }
	}
	break ;
      }

  messfree (p) ;

  /*
   * remember that response came from ac_partial_command, which is
   * returning a buffer that came out of the RPC library
   */
  free (response_free) ;
}

/******************************************************************/
/* PUBLIC ac_next_obj ()
 *
 * Return the next object in an iterator.
 *
 * The iterator contains a read-ahead buffer AC_ITER_OBJECT_READAHEAD
 * objects long.  When we fill the read-ahead, we get references
 * to a bunch of objects.  As we return each object, the
 * reference becomes the property of the application.  We still
 * own the reference for objects that we have not returned, so if
 * we destroy the iterator early, we have to ac_free the un-used
 * objects.
 * 
 */

AC_OBJ ac_next_obj (AC_ITER it)
{

  /*
   * Return NULL if no more objects
   */
  if (it->next > it->max)
    return NULL ;

  /*
   * if the read-ahead buffer is empty, fill it
   */
  if (it->ra_next >= it->ra_max) 
    {

      it->ra_next = 0 ;
      it->ra_max = 0 ;

      /*
       * load the keyset of the iterator.  next we will either
       * "list" or "show" from the active keyset.
       */
      ac_kget (it->real_keyset) ;


      if (it->fillhint)
	ac_iter_show (it) ;
      else
	ac_iter_list (it) ;
    }

  /*
   * return one object from the read-ahead if we have one
   */
  if (it->ra_next < it->ra_max)
    {
      AC_OBJ o ;
      o = it->obj[it->ra_next] ;
      it->obj[it->ra_next] = 0 ;	/* not really necessary */
      it->ra_next++ ;
      it->next++ ;
      return o ;
    }

  /*
   * if we get here, we tried to fill the read-ahead and it was still
   * empty.  We are at the end.
   */
  return NULL ;

} /* ac_next */

/****************************************************************/

/* PUBLIC ac_iter_rewind ()
 *
 * Set the iterator back to the beginning.  This includes freeing
 * any objects in the read-ahead.  
 *
 */

BOOL ac_iter_rewind (AC_ITER it) 
{
  int x ;
  for (x = it->ra_next ; x < it->ra_max ; x++)
    ac_free (it->obj[it->ra_next]) ;
  it->ra_next = 0 ;
  it->ra_max = 0 ;
  it->next = 0 ;
  return TRUE ;
} /* ac_rewind */

/****************************************************************/

/*
 * PUBLIC ac_read_keyset
 *
 * read/write a keyset in non-volatile storage.  This is here so we
 * can preserve keysets from one invocation of a cgi to the next.
 *
 * In the current client implementation, name is a file name on the 
 * server's disk.  The cgi programs take advantage of that by also
 * having an external program clean up old keysets.
 */

AC_KEYSET ac_read_keyset (AC_DB db, char *name, AC_HANDLE handle)
{
  AC_HANDLE h = ac_new_handle () ;
  unsigned char *r ;
  struct ac_keyset *aks ; 
  int max ;
  char *command = messalloc (60 + strlen (name)) ;

  sprintf (command, "keyset-read %s" , name) ;
  r = ac_command (db,  command, 0, NULL) ;

  max = active_objects_count (r) ;

  messfree (r) ;

  if (max == -1)
    return NULL ;

  aks = empty_keyset_struct (db, handle) ;

  aks->x->max = max ;

  sprintf (command, "kstore _aks%d" ,  aks->x->id) ;
  ac_command (db, command, 0, h)  ;

  messfree (command) ;
  handleDestroy (h) ;
  return (AC_KEYSET) aks ;
} /* ac_read_keyset */

/******************************************************************/
/*
 * PUBLIC ac_keyset_write
 */

BOOL ac_keyset_write (AC_KEYSET ks, char *name)
{
  char *command = messalloc (strlen(name)+100) ; 
  unsigned char *r ;

  if (!ks) return FALSE ;
  ac_kget (ks->x) ;

  /*
   * Strictly speaking, we could delay this command, but because it writes
   * non-volatile data to disk I want it to happen immediately.
   */
  sprintf (command, "list -a -f %s", name) ;
  r = ac_command (ks->x->db, command, NULL, NULL) ;
  messfree (r) ;
  messfree (command) ;

  return TRUE ;
} /* ac_write_keyset */

/****************************************************************/
/****************************************************************/
/****************************************************************/

/*
 * PUBLIC ac_keyset_table
 *
 * Create a 1 column table containing all objects in the keyset.
 */

AC_TABLE ac_keyset_table ( AC_KEYSET ks, int min, int max, int fillhint, AC_HANDLE handle )
{
  AC_TABLE tbl ;
  AC_ITER i ;
  int x ;

  if (max == -1)
    max = ks->x->max ;

  tbl = ac_empty_table (max, 1, handle) ;

  /*
   * create an iterator.  we cheat by poking our min value
   * into the iterator at the start.  We can do this because
   * the read-ahead buffer is still empty.
   */
  i = ac_keyset_iter (ks, fillhint, 0) ;
  i->next = min ;


  /*
   * fetch objects from min to max and put them in the table.
   */
  x = min ;
  while (x < max)
    {
      AC_OBJ o ;
      o = ac_next_obj (i) ;
      if (!o)
	break ;	/* never happens */
      ac_table_insert_type (tbl, x, 0, &o, ac_type_obj) ;
      ac_free (o) ;
      x++ ;
    }

  ac_free (i) ;

  return tbl ;
}

/******************************************************************/
/* PUBLIC
 * performs a command, creates a keyset of the result - this is to get
 * keysets from commands that are not part of the library
 */
AC_KEYSET ac_command_keyset (AC_DB db, char *command, AC_KEYSET iks, 
	AC_HANDLE handle)
{  
  unsigned char *r = 0 ;
  char buff[100] ;
  struct ac_keyset *ks = empty_keyset_struct (db, handle) ;

  if (iks)
    ac_kget (iks->x) ;

  db->lazy_command (db, command) ;

  sprintf (buff, "kstore _aks%d" ,  ks->x->id) ;
  r = ac_command (db, buff, 0, 0) ;
  ks->x->max = active_objects_count (r) ;
  messfree (r) ;
  return (AC_KEYSET) ks ;
} /* ac_query_keyset */


unsigned char *ac_a_data (AC_OBJ obj, int *lenp, AC_HANDLE handle)
{
  unsigned char *buf ;
  int len ;

  if (! obj->x->filled )
    ac_fillobj (obj) ;
  len = obj->x->a_data_len ;
  if (lenp)
    *lenp = len ;
  buf = (unsigned char *) halloc (len + 1 , handle) ;
  if (len)
    memcpy (buf, obj->x->a_data, len) ;
  return buf ;
}

/* PUBLIC
   * INCOMPLETE unless you add code in dump.c !!
   * nothing will get exported except for DNA Peptide

* returns a DNA or peptide sequence (according to seq_type).
* The returned string contains only the letters of the
* sequence.  If result_length is not NULL, it is filled in
* with the length of the sequence.
*/

/******************************************************************/
enum ac_sequence_type { ac_sequence_dna, ac_sequence_peptide };

	/*
	* returns a DNA or peptide sequence (according to seq_type).
	* The returned string contains only the letters of the
	* sequence.  If result_length is not NULL, it is filled in
	* with the length of the sequence.
	*
	* messfree() the returned string when you are finished with it.
	*/

static char *ac_sequence (AC_OBJ obj, int *result_length, 
			    enum ac_sequence_type seq_type, 
			    int x1, int x2, AC_HANDLE h)
{
  unsigned char *seq, *in, *out ;
  int n ;
  char command [256] ;

  n = find_one ( obj->x->db, obj->x->class, obj->x->name ) ;
  if (!n)
    return NULL ;
  switch (seq_type)
    {
    case ac_sequence_dna:
      if (x1 || x2)
	sprintf (command, "dna -x1 %d -x2 %d", x1, x2) ;
      else
	sprintf (command, "dna") ;
      seq = ac_command (obj->x->db, command, NULL, h) ;
      break ;
    case ac_sequence_peptide:
      seq = ac_command (obj->x->db, "peptide", NULL, h) ;
      break ;
    default:
      return NULL ;
    }
  
  out = seq ;
  in = seq ;
  while (isspace (*in))
    in++ ;
  if (*in != '>')
    goto bad_resp ;
  
  in = (unsigned char *)strchr ((char *)in, '\n') ;
  if (!in)
    goto bad_resp ;
  
  in++ ;
  
  while (*in)
    {
      if (*in == '/')
	break ;
      if (isspace (*in))
	{
	  in++ ;
	  continue ;
	}
      if (isalpha (*in))
	{
	  *out++ = *in++ ;
	  continue ;
	}
      goto bad_resp ;
    }
  
  *out = 0 ;
  
  if (result_length)
    * result_length = strlen ((char *)seq) ;
  
  return (char *)seq ;
  
 bad_resp:
  messerror ("No sequence available for %s\n", ac_name(obj)) ;
  return NULL ;
}

/******************************************************************/
/* PUBLIC */

char *ac_obj_peptide (AC_OBJ obj, AC_HANDLE h)
{
  int result_length = 0 ;
  return ac_sequence (obj, &result_length, ac_sequence_peptide, 0, 0, h) ;
}

char *ac_obj_dna (AC_OBJ obj, AC_HANDLE h)
{
  int result_length = 0 ;
  return ac_sequence (obj, &result_length, ac_sequence_dna, 0, 0, h) ;
}

char *ac_zone_dna (AC_OBJ obj, int x1, int x2, AC_HANDLE h)
{
  int result_length = 0 ;
  return ac_sequence (obj, &result_length, ac_sequence_dna, x1, x2, h) ;
}

char *ac_longtext (AC_OBJ obj, AC_HANDLE h)
{
  int n = find_one ( obj->x->db, obj->x->class, obj->x->name ) ;
  char* lt ;
  char *cp, *cq ;
  
  if (!n)
    return NULL ;
 
  lt = (char *) ac_command (obj->x->db, "show", NULL, h) ;
  if ((cp = strstr (lt, "***LongTextEnd***")))
    *cp = 0 ;
  else
    return lt ;
   cp = strstr (lt, "LongText") ;
  while (*cp && *cp != '\n') cp++ ; /* jump tiltle line */
  if (strlen (cp))
    cq = strnew (cp, h) ;
  else
    cq = 0 ;
  messfree (lt) ;
  return cq ;
}

/******************************************************************/
/* PUBLIC */

BOOL ac_parse ( AC_DB db, char *text, char **error_messages,  
		AC_KEYSET *nks, AC_HANDLE h )
{
  char *s, *s1 ;
  unsigned char *s2 ;
  int len ;
  AC_KEYSET ks = 0 ;
  char buff[100] ;
  BOOL success = TRUE ;
  
  len = strlen (text) + 100 ;
  s = halloc (len, NULL_HANDLE) ;
  strcpy (s, "serverparse\n") ;
  strcat (s, text) ;
  
  /*
   * create the new keyset first, because it destroys the
   * server's idea of the active keyset
   */
  if (nks)
    ks = ac_new_keyset (db, NULL, h) ;
  
  s1 = (char *) ac_command (db, s, NULL, h) ;
  
  if (strstr (s1, "error"))
    success=FALSE ;
  
  if (error_messages)
    *error_messages = s1 ;
  else
    messfree (s1) ;
  
  if (nks)
    {
      sprintf (buff, "kstore _aks%d" ,  ks->x->id) ;
      s2 = ac_command (db, buff, 0, 0) ;
      ks->x->max = active_objects_count (s2) ;
      messfree (s2) ;
      *nks = ks ;
    }
  
  messfree (s) ;
  return success ;
}

/******************************************************************/
/******************************************************************/
