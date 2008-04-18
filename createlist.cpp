#include <grace/application.h>
#include "limadb.h"

#include "paths.h"

class createlistApp : public application
{
public:
	createlistApp (void) : application ("com.openpanel.tools.limacreate")
	{
	}
	~createlistApp (void)
	{
	}
	
	int main (void)
	{
		if (argv["*"].count() < 2)
		{
			usage ();
			return 1;
		}
		
		string listid = argv["*"][0];
		string listowner = argv["*"][1];
		string name;
		string subjecttag;
		bool moderatedsubs = false;
		bool moderatedposts = false;
		if (argv.exists ("--name"))
		{
			name = argv["--name"];
		}
		else
		{
			name = listid.copyuntil ('@');
		}
		if (argv.exists ("--subjecttag"))
		{
			subjecttag = argv["--subjecttag"];
		}
		else
		{
			subjecttag = name;
		}
		if (argv.exists ("--moderate-subscriptions"))
		{
			moderatedsubs = true;
		}
		if (argv.exists ("--moderate-posts"))
		{
			moderatedposts = true;
		}
		
		limadb mydb (PATH_LIMADB);
		
		if (mydb.listexists (listid))
		{
			ferr.printf ("List already exists\n");
			return 1;
		}
		
		if (! mydb.createlist (listid, listowner, moderatedposts,
							   moderatedsubs, name, subjecttag))
		{
			return 1;
		}
		
		mailinglist mlist (mydb, listid);
		listmember &m = mlist.createmember (listowner);
		m.approved (true);
		m.moderator (true);
		
		return 0;
	}
	
	void usage (void)
	{
		ferr.printf ("Usage: lima-create <list-address> <owner-address> [options]\n");
		ferr.printf ("       --name <name>            Short list name\n"
					 "       --subjecttag <tag>       Tag for message subjects\n"
					 "       --moderate-subscriptions Subscription requests get queued\n"
					 "       --moderate-posts         Moderate posts\n");
	}
};

APPOBJECT(createlistApp);
