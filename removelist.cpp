// This file is part of OpenPanel - The Open Source Control Panel
// OpenPanel is free software: you can redistribute it and/or modify it 
// under the terms of the GNU General Public License as published by the Free 
// Software Foundation, using version 3 of the License.
//
// Please note that use of the OpenPanel trademark may be subject to additional 
// restrictions. For more information, please visit the Legal Information 
// section of the OpenPanel website on http://www.openpanel.com/

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
