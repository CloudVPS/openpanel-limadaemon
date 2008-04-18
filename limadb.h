#ifndef _LIMAD_LIMADB_H
#define _LIMAD_LIMADB_H 1

#include <grace/exception.h>
#include <grace/value.h>
#include <grace/statstring.h>
#include <querido/query.h>
#include <querido/table.h>

THROWS_EXCEPTION (
	randomDeviceException,
	0x343cd0d4,
	"Could not access /dev/urandom."
);

THROWS_EXCEPTION (
	tableCreateException,
	0x2e8373c7,
	"Error creating SQL tables"
);

//  -------------------------------------------------------------------------
/// Top level database proxy object, container for the involved SQL tables.
//  -------------------------------------------------------------------------
class limadb
{
public:
						 limadb (const string &pbase);
						~limadb (void);
	
	bool				 listexists (const statstring &listid);
	
	bool				 createlist (const statstring &listid,
									 const string &owner,
									 bool usemoderation = false,
									 bool usesubmoderation = false,
									 const string &listname="",
									 const string &subjecttag="");
									 
	bool				 deletelist (const statstring &listid);
	
	bool				 loadlist (const statstring &listid);
	
	dbengine			 DB;
	dbtable				 tlists;
	dbtable				 tmembers;

protected:
	string				 basepath;
	
	string				*gensalt (void);
};

THROWS_EXCEPTION (
	existentialListException,
	0xd5d64a00,
	"List does not exist");

THROWS_EXCEPTION (
	notImplementedException,
	0x40c04211,
	"Functionality not implemented");
	
THROWS_EXCEPTION (
	existentialMemberException,
	0xdf425079,
	"Member does not exist");
	
THROWS_EXCEPTION (
	defaultConstructorException,
	0x63f07b6f,
	"Illegal default constructor");

//  -------------------------------------------------------------------------
/// Proxy object representing a list member in the SQL database.
//  -------------------------------------------------------------------------
class listmember
{
public:
						 listmember (void);
						 listmember (const string &paddress,
						 			 class mailinglist *plist);
						 listmember (listmember *orig);
						 listmember (listmember &orig);
						~listmember (void);
						
	const string		&address (void) { return _address.sval(); }
	
	bool				 approved (void);
	void				 approved (bool);
	bool				 moderator (void);
	void				 moderator (bool);
	bool				 needsprobe (void);
	void				 needsprobe (bool);
	
	void				 password (const string &);
	bool				 verifypassword (const string &);
	
protected:
	class mailinglist	&list;
	statstring			_address;
	time_t				 lastload;
	value				 mdata;
	
	void				 load (void);
	void				 update (const value &);
};

typedef dictionary<listmember> memberlist;

enum postrestriction_t
{
	POST_OPEN = 0,
	POST_MEMBERSONLY,
	POST_MODERATED
};

THROWS_EXCEPTION (
	mailinglistLoadFromDBException,
	0x71f59b07,
	"Error loading mailinglist info from database"
);

//  -------------------------------------------------------------------------
/// Proxy object representing a mailinglist in the SQL database.
//  -------------------------------------------------------------------------
class mailinglist
{
						 friend class listmember;
public:
						 mailinglist (void);
						 mailinglist (limadb &wot, const statstring &id);
						 mailinglist (mailinglist *orig);
						~mailinglist (void);
	
	const statstring	&id (void) { return _id; }
	
	postrestriction_t	 moderatedposts (void);
	void				 moderatedposts (postrestriction_t);
	bool				 moderatedsubs (void);
	void				 moderatedsubs (bool);
	bool				 replytoheader (void);
	void				 replytoheader (bool);
	string				*subjecttag (void);
	void				 subjecttag (const string &);
	string				*name (void);
	void				 name (const string &);
	string				*owner (void);
	void				 owner (const string &);
	
	string				*salt (void);
	
	value				*listmoderators (void);
	value				*listmembers (int offset=0, int amount=0);
	value				*listmembermodq (void);
	value				*dump (void);
	
	bool				 memberexists (const statstring &address);
	
	void				 approvemember (const statstring &address);
	void				 disapprovemember (const statstring &address);
	
	void				 deletemember (const statstring &address);
	listmember			&createmember (const statstring &address);
	listmember			&getmember (const statstring &address);
	
protected:
	value				 loaded;
	statstring			 dbid;
	statstring			_id;
	limadb				&ldb;
	memberlist			 members;
};

#endif
