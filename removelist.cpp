#include <grace/application.h>
#include "limadb.h"
#include "paths.h"

class removelistApp : public application
{
public:
	removelistApp (void) : application ("com.openpanel.tools.limaremove")
	{
	}
	~removelistApp (void)
	{
	}
	
	int main (void)
	{
		if (argv["*"].count() < 1)
		{
			ferr.printf ("%% Usage: lima-remove <listid>\n");
			return 1;
		}
		
		string listid = argv["*"][0];
		limadb mydb (PATH_LIMADB);
		if (! mydb.deletelist (listid))
		{
			ferr.printf ("%% Error removing list <%s>\n", listid.str());
			return 1;
		}
		
		return 0;
	}
};

APPOBJECT(removelistApp);
