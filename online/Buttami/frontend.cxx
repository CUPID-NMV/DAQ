/********************************************************************\

  Name:         frontend.c
  Created by:   Stefan Ritt

  Contents:     Experiment specific readout code (user part) of
                Midas frontend. This example simulates a "trigger
                event" and a "periodic event" which are filled with
                random data.
 
                The trigger event is filled with two banks (ADC0 and TDC0),
                both with values with a gaussian distribution between
                0 and 4096. About 100 event are produced per second.
 
                The periodic event contains one bank (PRDC) with four
                sine-wave values with a period of one minute. The
                periodic event is produced once per second and can
                be viewed in the history system.

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h> // assert()

#include "midas.h"
#include "experim.h"

#include "mfe.h"

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "Sample Frontend";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/*-- Function declarations -----------------------------------------*/

INT read_trigger_event(char *pevent, INT off);
INT read_periodic_event(char *pevent, INT off);

INT poll_trigger_event(INT source, INT count, BOOL test);

/*-- Equipment list ------------------------------------------------*/

BOOL equipment_common_overwrite = TRUE;

EQUIPMENT equipment[] = {

   {"Trigger",               /* equipment name */
      {1, 0,                 /* event ID, trigger mask */
         "SYSTEM",           /* event buffer */
         EQ_POLLED,          /* equipment type */
         0,                  /* event source */
         "MIDAS",            /* format */
         TRUE,               /* enabled */
         RO_RUNNING |        /* read only when running */
         RO_ODB,             /* and update ODB */
         100,                /* poll for 100ms */
         0,                  /* stop run after this event limit */
         0,                  /* number of sub events */
         0,                  /* don't log history */
         "", "", "", "", "", 0, 0},
      read_trigger_event,    /* readout routine */
   },

   {"Periodic",              /* equipment name */
      {2, 0,                 /* event ID, trigger mask */
         "SYSTEM",           /* event buffer */
         EQ_PERIODIC,        /* equipment type */
         0,                  /* event source */
         "MIDAS",            /* format */
         TRUE,               /* enabled */
         RO_RUNNING | RO_TRANSITIONS |   /* read when running and on transitions */
         RO_ODB,             /* and update ODB */
         1000,               /* read every 1000 msec (1 sec) */
         0,                  /* stop run after this event limit */
         0,                  /* number of sub events */
         10,                 /* log history every ten seconds*/
       "", "", "", "", "", 0, 0},
      read_periodic_event,   /* readout routine */
   },

   {""}
};

/********************************************************************\
                  Frontend callback routines

  The function frontend_init gets called when the frontend program
  is started. This routine should initialize the hardware, and can
  optionally install several callback functions:

  install_poll_event:
     Install a function which gets called to check if a new event is
     available for equipment of type EQ_POLLED.

  install_frontend_exit:
     Install a function which gets called when the frontend program
     finishes.

   install_begin_of_run:
      Install a function which gets called when a new run gets started.

   install_end_of_run:
      Install a function which gets called when a new run gets stopped.

   install_pause_run:
      Install a function which gets called when a new run gets paused.

   install_resume_run:
      Install a function which gets called when a new run gets resumed.

   install_frontend_loop:
      Install a function which gets called inside the main event loop
      as often as possible. This function gets all available CPU cycles,
      so in order not to take 100% CPU, this function can use the
      ss_sleep(10) function to give up some CPU cycles.

 \********************************************************************/

/*-- Frontend Init -------------------------------------------------*/

INT frontend_init()
{
   /* install polling routine */
   install_poll_event(poll_trigger_event);

   /* put any hardware initialization here */

   /* print message and return FE_ERR_HW if frontend should not be started */
   return SUCCESS;
}

/*------------------------------------------------------------------*/

/********************************************************************\

  Readout routines for different events

\********************************************************************/

/*-- Trigger event routines ----------------------------------------*/

INT poll_trigger_event(INT source, INT count, BOOL test)
/* Polling routine for events. Returns TRUE if event
   is available. If test equals TRUE, don't return. The test
   flag is used to time the polling */
{
   int i;
   DWORD flag;

   for (i = 0; i < count; i++) {
      /* poll hardware and set flag to TRUE if new event is available */
      flag = TRUE;

      if (flag)
         if (!test)
            return TRUE;
   }

   return 0;
}

/*-- Event readout -------------------------------------------------*/
// This function gets called whenever poll_event() returns TRUE (the
// MFE framework calls poll_event() regularly).

INT read_trigger_event(char *pevent, INT off)
{
   UINT32 *pdata;

   /* init bank structure */
   bk_init(pevent);

   /* create a bank called ADC0 */
   bk_create(pevent, "ADC0", TID_UINT32, (void **)&pdata);

   /* following code "simulates" some ADC data */
   for (int i = 0; i < 4; i++)
      *pdata++ = rand()%1024 + rand()%1024 + rand()%1024 + rand()%1024;

   bk_close(pevent, pdata);

   /* create another bank called TDC0 */
   bk_create(pevent, "TDC0", TID_UINT32, (void **)&pdata);

   /* following code "simulates" some TDC data */
   for (int i = 0; i < 4; i++)
      *pdata++ = rand()%1024 + rand()%1024 + rand()%1024 + rand()%1024;

   bk_close(pevent, pdata);

   /* limit event rate to 100 Hz. In a real experiment remove this line */
   ss_sleep(10);

   return bk_size(pevent);
}

/*-- Periodic event ------------------------------------------------*/
// This function gets called periodically by the MFE framework (the
// period is set in the EQUIPMENT structs at the top of the file).

INT read_periodic_event(char *pevent, INT off)
{
   UINT32 *pdata;

   /* init bank structure */
   bk_init(pevent);

   /* create a bank called PRDC */
   bk_create(pevent, "PRDC", TID_UINT32, (void **)&pdata);

   /* following code "simulates" some values in sine wave form */
   for (int i = 0; i < 16; i++)
      *pdata++ = 100*sin(M_PI*time(NULL)/60+i/2.0)+100;

   bk_close(pevent, pdata);

   return bk_size(pevent);
}
