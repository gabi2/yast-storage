
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/statvfs.h>
#include "Y2PartedComponent.h"
#include "WIN_Partition.h"
#include <ycp/y2log.h>
#include <y2/Y2ComponentBroker.h>
#include <y2/Y2Component.h>
#include <y2/Y2Namespace.h>
#include <y2/Y2Function.h>


#define RETURN_OK      YCPSymbol("ok")     	// `ok
#define RETURN_ERROR   YCPSymbol("error")	// `error
#define RETURN_CANCEL  YCPSymbol("cancel")	// `cancel


// PartedComponent work function
// Do the work when the parted client module is called.
// Returns:	`ok	- OK
//		`error	- an error occured
// These values are returned to the YCP-script
//
YCPValue
Y2PartedComponent::doActualWork(const YCPList& options, Y2Component *displayserver)
{
   string message_progress("");
   string message_directory("");
   string message_exception("");

   YCPValue returncode 	= RETURN_OK;
   YCPValue val 	= YCPVoid();




   // get the parameters from the YCP level as:
   // <progress_macro>		- macro to adjust progress bar
   // <directory_macro>		- macro to display directory information
   // <exception_macro>		- macro to display exception information
   // "/dev/sda1"		- partition to be resized
   // "0.0"			- new start of partition in MB on disk
   // "200.3"			- length of partition im MB on disk

   report_macro = NULL;

   // get the progress macro
   string macro = (options->value(0)->isVoid() ? ""
		      : options->value(0)->asString()->value());

   string::size_type colonpos = macro.find("::");
   if( colonpos != string::npos ) 
       {
       module = macro.substr ( 0, colonpos );
       progress_symbol = macro.substr ( colonpos + 2 );
       Y2Component* comp = 
	   Y2ComponentBroker::getNamespaceComponent( module.c_str() );
       if( comp != NULL )
	   {
	   report_macro = comp->import( module.c_str() );
	   if( report_macro == NULL )
	       {
	       y2error( "Component does does not provide %s", module.c_str() );
	       }
	   }
       else
	   {
	   y2error( "Cannot find a component to provide %s", module.c_str() );
	   }
       }

   // get the directory symbol
   macro = (options->value(1)->isVoid() ? ""
	       : options->value(1)->asString()->value());
   colonpos = macro.find("::");
   if( colonpos != string::npos ) 
       {
       directory_symbol = macro.substr ( colonpos + 2 );
       }

   // get the exception symbol
   macro = (options->value(2)->isVoid() ? ""
	       : options->value(2)->asString()->value());
   colonpos = macro.find("::");
   if( colonpos != string::npos ) 
       {
       exception_symbol = macro.substr ( colonpos + 2 );
       }

   // get the partition
   partition = (options->value(3)->isVoid() ? ""
		: options->value(3)->asString()->value());

   // get the starting point in MB on disk
   partition_start = (options->value(4)->isVoid() ? ""
		      : options->value(4)->asString()->value());

   // get the new_size ( must be valid )
   partition_length = (options->value(5)->isVoid() ? ""
		       : options->value(5)->asString()->value());

   //
   // start work
   //
   y2milestone ("Resizing partition: <%s>", partition.c_str());


   // create partition to be resized
   WIN_Partition win_p( partition );

   // init progress bar
   val = report_progress( displayserver, "0.0" );

   if ( !val->isVoid() ) return RETURN_ERROR;	// problem with display server

   // Start the resize process (fork the parted process)
   win_p.resize( partition_start, partition_length );

   // show progress
   while ( win_p.get_progress_status( message_progress,
				      message_directory,
				      message_exception ) )
   {
      if ( message_progress != "" )
      {
	 // adjust the progress bar using the downloaded progress macro
	 val = report_progress( displayserver, message_progress );

	 if ( !val->isVoid() )
	 {
	    y2error ("report_progress() returned <%s>", val->toString().c_str() );
	 }
      }

      if ( message_directory != "" )
      {
	 // display current directory  using the downloaded directory macro
	 val = report_directory( displayserver, message_directory );

	 if ( !val->isVoid() )
	 {
	    y2error ("report_directory() returned <%s>", val->toString().c_str() );
	 }
      }

      if ( message_exception != "" )
      {
	 // display current exception  using the downloaded exception macro
	 val = report_exception( displayserver, message_exception );

	 if ( !val->isVoid() )
	 {
	    y2error ("report_exception() returned <%s>", val->toString().c_str() );
	 }
      }

      // clear message strings for next run
      message_progress  = "";
      message_directory = "";
      message_exception = "";
   }

   int parted_status = win_p.status();

   if ( parted_status != 0)	// parted reported an error
   {
      y2warning ("parted returned <%d>", parted_status);
      returncode = RETURN_ERROR;
   }

   return returncode;
}	// end of DoActualWork()

//
// Adjust the progress bar using the downloaded progress macro.
// Returns:	YCPVoid	- OK or no macro available
//		`error	- display server reported an error
//
YCPValue
Y2PartedComponent::report_progress(Y2Component *displayserver,
				   string message_progress )
    {
    YCPValue val = YCPVoid();
    Y2Function* t = NULL;
    // get the percent value from the string
    long long percent = atol( message_progress.c_str() );

    y2debug ("Reporting progress: <%lld>", percent );

    if( report_macro ) 
        {
        // build command
        t = report_macro->createFunctionCall( progress_symbol, Type::Unspec );
        }
    if( report_macro && t )
	{
	t->appendParameter( YCPInteger( percent ) ); // progress
	YCPValue val = t->evaluateCall ();
	delete t;

        // check result
	if( !val->isVoid() )
	    {
	    y2error( "displayserver(progress) returned <%s>", 
	             val->toString().c_str() );
	    return RETURN_ERROR;
	    }
	}
    return val;
    }	// End of report_progress()


//
// Display directory information using the downloaded directory macro.
// Returns:	YCPVoid	- OK or no macro available
//		`error	- display server reported an error
//
YCPValue
Y2PartedComponent::report_directory(Y2Component *displayserver,
				    string message_directory )
    {
    YCPValue val = YCPVoid();
    Y2Function* t = NULL;

    if( report_macro ) 
	{
	// build command
	t = report_macro->createFunctionCall (directory_symbol, Type::Unspec);
	}

    // if no macro at hand return immediately
    if( report_macro && t )
	{
	// filter out garbage
	const char* message_buf = message_directory.c_str();
	char  message_final[64];	// should be enough in any case

	int length = strlen( message_buf );
	int i = 0;
	int k = 0;

	memset( message_final, 0, 64 );

	for( i = 0, k = 0; i < length && k < 8; i++ )
	    {
	    if( isalnum( (int) message_buf[i] ) || 
		index( ".-_", (int) message_buf[i] ) )
		{
		message_final[k++] = message_buf[i];
		}
	    }
	message_final[k++] = '.';
	message_final[k++] = '.';
	message_final[k++] = '.';
	message_final[k++] = '\n';

	y2debug ("Reporting directory: <%s>", message_final );

	t->appendParameter( YCPString( string(message_final) ) ); // directory
	YCPValue val = t->evaluateCall ();
	delete t;

	// check result
	if( !val->isVoid() )
	    {
	    y2error( "displayserver(directory) returned <%s>", 
		     val->toString().c_str() );
	    return RETURN_ERROR;
	    }
	}
     return val;
     }	// End of report_directory()



//
// Display exception output from parted using the downloaded exception macro.
// Returns:	YCPVoid	- OK or no macro available
//		`error	- display server reported an error
//
YCPValue
Y2PartedComponent::report_exception(Y2Component *displayserver,
				   string message_exception )
    {
    YCPValue val = YCPVoid();
    Y2Function* t = NULL;

    y2debug ("Reporting exception: <%s>", message_exception.c_str() );
    if( report_macro ) 
	{
	// build command
	t = report_macro->createFunctionCall( exception_symbol, Type::Unspec );
	}

    // if no macro at hand return immediately
    if( report_macro && t )
	{
	t->appendParameter( YCPString( message_exception ) ); // excpetion
	YCPValue val = t->evaluateCall ();
	delete t;

	// check result
	if( !val->isVoid() )
	    {
	    y2error( "displayserver(exception) returned <%s>", 
		     val->toString().c_str() );
	    return RETURN_ERROR;
	    }
	}

    return val;
    }	// End of report_exception()
