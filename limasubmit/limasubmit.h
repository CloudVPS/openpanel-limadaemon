// This file is part of OpenPanel - The Open Source Control Panel
// OpenPanel is free software: you can redistribute it and/or modify it 
// under the terms of the GNU General Public License as published by the Free 
// Software Foundation, using version 3 of the License.
//
// Please note that use of the OpenPanel trademark may be subject to additional 
// restrictions. For more information, please visit the Legal Information 
// section of the OpenPanel website on http://www.openpanel.com/

#ifndef _limasubmit_H
#define _limasubmit_H 1
#include <grace/application.h>

#include "../paths.h"

//  -------------------------------------------------------------------------
/// Main application class.
//  -------------------------------------------------------------------------
class limasubmitApp : public application
{
public:
		 	 limasubmitApp (void) :
				application ("com.openpanel.tools.limasubmit")
			 {
			 }
			~limasubmitApp (void)
			 {
			 }

	int		 main (void);
	bool	 sendmail (const string &sender, const string &rcpt,
					   const string &data);
	
	void	 enqueue (const string &msgdata, const string &listname,
					  const string &subjtag, bool setreplyto = false);
};

#endif
