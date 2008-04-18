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
