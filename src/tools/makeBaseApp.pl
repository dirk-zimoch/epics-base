#!/usr/local/bin/perl
#
#	Author: Kay-Uwe Kasemir,
#	based on Marty Kraimer's makeBaseApp
#
#	1-27-97
#
#	Notes:
#	1) The perl location /usr/local/bin/perl may be wrong,
#	   the safest way to run this script is via a direct
#		perl <script>
#	2) You need Perl 5.x to run this.
#	   Get it from http://www.perl.com if you don't have it, yet.

#	These extensions come with the basic perl distribution
#	and ran on sun4 and WIN32:
use Cwd;
use Getopt::Std;
use File::Copy;

$startdir=cwd();
#	remove auto-mount path which confuses directory name comparison
$startdir=~s'^/tmp_mnt'';

Cleanup (1, "Shouldn't run in Home Directory") if (GetHome() eq $startdir);
print "I hope that '$startdir' is NOT your home directory\n";


#	look for options
getopt("b");
if ($opt_e)
{
	$example=1;
}

$user=GetUser();
@apps=@ARGV;

#	if this script was called with full path to base,
#	extract EPICS/base.
#	(this failes for '/base/' starting at root but that's unlikely)
if ($0 =~ m'(/.*/base)/bin/')
{
	$epics_base = $1;
}
else
{
	$epics_base=$opt_b if ($opt_b);
}

Cleanup (1,"Cannot find EPICS base, use full path or '-b'") unless $epics_base;

print "Found EPICS base: $epics_base\n";

unless (-f 'Makefile')
{
	print "Creating Makefile\n";

	open OUT, ">Makefile" or die "Cannot open Makefile";
	print OUT "#\tMakefile generated by makeBaseApp.pl\n";
	print OUT "TOP = .\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "DIRS += \$(wildcard *App)\n";
	print OUT "DIRS += iocBoot\n";
	print OUT "include \$(TOP)/config/RULES_TOP\n";
	close OUT;
}

unless (-d 'config')
{
	mkdir 'config', 0777;
	chdir 'config' or die "Cannot create config directory";

	print "Creating files in 'config'\n";

	open OUT, ">CONFIG" or die "Cannot create CONFIG";
	print OUT "#\tCONFIG\n";
	print OUT "# Add any changes to make rules here\n";
	print OUT "#CROSS_COMPILER_TARGET_ARCHS = mv167\n";
	close OUT;

	open OUT, ">CONFIG_APP" or die "Cannot create CONFIG_APP";
	print OUT "#\tCONFIG_APP DO NOT EDIT THIS FILE\n";
	print OUT "include \$(TOP)/config/RELEASE\n";
	print OUT "ifdef SHARE\n";
	print OUT "include $(SHARE)/config/CONFIG_APP\n";
	print OUT "else\n";
	print OUT "ifndef T_A\n";
	print OUT "T_A = \$(HOST_ARCH)\n";
	print OUT "endif #ifndef T_A\n";
	print OUT "include \$(EPICS_BASE)/config/CONFIG\n";
	print OUT "ifdef MASTER_IOCAPPS\n";
	print OUT "USR_INCLUDES += -I\$(MASTER_IOCAPPS)/include\n";
	print OUT "MASTER_IOCAPPS_BIN = \$(MASTER_IOCAPPS)/bin/\$(T_A)\n";
	print OUT "endif #ifdef MASTER_IOCAPPS\n";
	print OUT "include \$(TOP)/config/CONFIG\n";
	print OUT "endif #ifdef SHARE\n";
	close OUT;

	open OUT, ">RELEASE" or die "Cannot create RELEASE";
	print OUT "#\tRELEASE Location of external products\n";
	print OUT "EPICS_BASE=$epics_base\n";
	print OUT "#SHARE=\n";
	print OUT "#MASTER_IOCAPPS=\n";
	close OUT;

	open OUT, ">RULES.Vx" or die "Cannot create RULES.Vx";
	print OUT "#\tRULES.Vx\n";
	print OUT "include \$(EPICS_BASE)/config/RULES.Vx\n";
	print OUT "inc:: \$\(INSTALL_INCREC\)\n";
	close OUT;

	open OUT, ">RULES_TOP" or die "Cannot create RULES_TOP";
	print OUT "#\tRULES_TOP\n";
	print OUT "include \$(EPICS_BASE)/config/RULES_TOP\n";
	print OUT "\n";
	print OUT "ifdef MASTER_IOCAPPS\n";
	print OUT "inc:\tsoftlinks\n";
	print OUT "buildInstall:\tsoftlinks\n";
	print OUT "\n";
	print OUT "softlinks:\n";
	print OUT "\t";
	print OUT '@echo "softlinks" ; if [ "$(MASTER_IOCAPPS)" ]; then \\';
	print OUT "\n";
	print OUT "\t";
	print OUT 'for DIR in bin lib dbd include man ; do \\';
	print OUT "\n";
	print OUT "\t";
	print OUT '$(SHARE)/config/makeSoftLinks $(MASTER_IOCAPPS) $${DIR} ;\\';
	print OUT "\n";
	print OUT "\t";
	print OUT 'done; fi';
	print OUT "\n";
	print OUT "\n";
	print OUT ".PHONY :: softlinks\n";
	print OUT "endif\n";
	close OUT;

	#	Simple config files that just include the BASE version:
	foreach $file ( 'RULES.Db', 'RULES.Host', 'RULES.ioc',
					'RULES_ARCHS', 'RULES_DIRS')
	{
		open OUT, ">$file" or die "Cannot create $file";
		print OUT "#\t$file\n";
		print OUT "include \$(EPICS_BASE)/config/$file\n";
		close OUT;
	}
	chdir $startdir or die 'cannot return to start directory';
}

#	Generate app dirs (if any names given)
#
foreach $app ( @apps )
{
	print "Creating Application '$app'\n";
	print "with example\n" if $example;

	if (-d "${app}App")
	{
		print "App '$app' is already there!\n";
		next;
	}

	mkdir "${app}App", 0777;
	chdir "${app}App" or die "Cannor create dir ${app}App";

	open OUT, ">Makefile" or die "Cannot open Makefile";
	print OUT "TOP = ..\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "DIRS += \$(wildcard *src*)\n";
	print OUT "DIRS += \$(wildcard *Db*)\n";
	print OUT "include \$(TOP)/config/RULES_DIRS\n";
	close OUT;

	mkdir "Db", 0777;
	open OUT, ">Db/Makefile" or die "Cannot open Db/Makefile";
	print OUT "TOP = ../..\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "\n";
	print OUT "include \$(TOP)/config/RULES.Db\n";
	close OUT;

	if ($example)
	{
		open OUT, ">Db/dbExample.db" or
						die "Cannot open Db/dbExample.db";
		print OUT "record(ai, \"\$(user):aiExample\")\n";
		print OUT "{\n";
		print OUT "\tfield(DESC, \"Analog input\")\n";
		print OUT "\tfield(INP,  \"\$(user):calcExample.VAL NPP NMS\")\n";
		print OUT "\tfield(EGUF, \"10\")\n";
		print OUT "\tfield(EGU,  \"Counts\")\n";
		print OUT "\tfield(HOPR, \"10\")\n";
		print OUT "\tfield(LOPR, \"0\")\n";
		print OUT "\tfield(HIHI, \"8\")\n";
		print OUT "\tfield(HIGH, \"6\")\n";
		print OUT "\tfield(LOW,  \"4\")\n";
		print OUT "\tfield(LOLO, \"2\")\n";
		print OUT "\tfield(HHSV, \"MAJOR\")\n";
		print OUT "\tfield(HSV,  \"MINOR\")\n";
		print OUT "\tfield(LSV,  \"MINOR\")\n";
		print OUT "\tfield(LLSV, \"MAJOR\")\n";
		print OUT "}\n";
		print OUT "record(calc, \"\$(user):calcExample\")\n";
		print OUT "{\n";
		print OUT "\tfield(DESC, \"Counter\")\n";
		print OUT "\tfield(SCAN, \"1 second\")\n";
		print OUT "\tfield(FLNK, \"\$(user):aiExample\")\n";
		print OUT "\tfield(CALC, \"(A<B)?(A+C):D\")\n";
		print OUT "\tfield(INPA, \"\$(user):calcExample.VAL NPP NMS\")\n";
		print OUT "\tfield(INPB, \"9\")\n";
		print OUT "\tfield(INPC, \"1\")\n";
		print OUT "\tfield(INPD, \"0\")\n";
		print OUT "\tfield(EGU,  \"Counts\")\n";
		print OUT "\tfield(HOPR, \"10\")\n";
		print OUT "\tfield(HIHI, \"8\")\n";
		print OUT "\tfield(HIGH, \"6\")\n";
		print OUT "\tfield(LOW,  \"4\")\n";
		print OUT "\tfield(LOLO, \"2\")\n";
		print OUT "\tfield(HHSV, \"MAJOR\")\n";
		print OUT "\tfield(HSV,  \"MINOR\")\n";
		print OUT "\tfield(LSV,  \"MINOR\")\n";
		print OUT "\tfield(LLSV, \"MAJOR\")\n";
		print OUT "}\n";
		print OUT "record(xxx, \"\$(user):xxxExample\")\n";
		print OUT "{\n";
		print OUT "\tfield(DESC, \"xxx record\")\n";
		print OUT "\tfield(EGU,  \"Counts\")\n";
		print OUT "\tfield(HOPR, \"10\")\n";
		print OUT "\tfield(HIHI, \"8\")\n";
		print OUT "\tfield(HIGH, \"6\")\n";
		print OUT "\tfield(LOW,  \"4\")\n";
		print OUT "\tfield(LOLO, \"2\")\n";
		print OUT "\tfield(HHSV, \"MAJOR\")\n";
		print OUT "\tfield(HSV,  \"MINOR\")\n";
		print OUT "\tfield(LSV,  \"MINOR\")\n";
		print OUT "\tfield(LLSV, \"MAJOR\")\n";
		print OUT "}\n";
	}

	mkdir 'src', 0777;

	open OUT, ">src/Makefile" or die "Cannot open src/Makefile";
	print OUT "TOP=../..\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "include \$(TOP)/config/RULES_ARCHS\n";
	close OUT;

	open OUT, ">src/Makefile.Vx" or die "Cannot open src/Makefile.Vx";
	print OUT "# Makefile.Vx\n";
	print OUT "TOP = ../../..\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "#----------------------------------------\n";
	print OUT "#  ADD MACRO DEFINITIONS AFTER THIS LINE\n";

	# OUT still set to Makefile.Vx
	if ( $example)
	{
		print OUT "RECTYPES += xxxRecord.h\n";
		print OUT "USER_DBDFLAGS += -I .. -I \$(EPICS_BASE)/dbd\n";
		print OUT "DBDEXPAND = ${app}Include.dbd\n";
		print OUT "DBDNAME = ${app}App.dbd\n";
		print OUT "SRCS.c += ../xxxRecord.c\n";
		print OUT "SRCS.c += ../devXxxSoft.c\n";
		print OUT "\n";
		print OUT "include ../baseLIBOBJS\n";
		print OUT "LIBOBJS += xxxRecord.o\n";
		print OUT "LIBOBJS += devXxxSoft.o\n";
		print OUT "LIBOBJS += sncExample.o\n";
		print OUT "LIBNAME = ${app}Lib\n";
		print OUT "INSTALLS += vxWorks vxWorks.sym iocCore seq\n";
	}
	else
	{
		print OUT "#RECTYPES += anyRecord.h\n";
		print OUT "USER_DBDFLAGS += -I .. -I \$(EPICS_BASE)/dbd\n";
		print OUT "#DBDEXPAND = ${app}Include.dbd\n";
		print OUT "#DBDNAME = ${app}App.dbd\n";
		print OUT "#SRCS.c += ../anysource.c\n";
		print OUT "\n";
		print OUT "#include ../baseLIBOBJS\n";
		print OUT "#LIBOBJS += anysource.o\n";
		print OUT "#LIBNAME = ${app}Lib\n";
		print OUT "#INSTALLS += vxWorks vxWorks.sym iocCore seq\n";
	}

	# OUT still set to Makefile.Vx...
	print OUT "\n";
	print OUT "#\tNote that the command line that builds the\n";
	print OUT "#\tlibrary \$(LIBNAME) may be HUGE (>3kB)\n";
	print OUT "#\n";
	print OUT "\n";
	print OUT "include \$(TOP)/config/RULES.Vx\n";
	print OUT "#----------------------------------------\n";
	print OUT "#  ADD RULES AFTER THIS LINE\n";

	close OUT;

	open OUT, ">src/Makefile.Host"
		or die "Cannot open src/Makefile.Host";

	print OUT "# Makefile.Host\n";
	print OUT "TOP = ../../..\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "#----------------------------------------\n";
	print OUT "#  ADD MACRO DEFINITIONS AFTER THIS LINE\n";
	print OUT "\n";
	print OUT "#USR_CFLAGS += \n";
	print OUT "PROD_LIBS_DEFAULT += m\n";
	print OUT "PROD_LIBS_WIN32 := -nil-\n";
	print OUT "PROD_LIBS	+= ca\n";
	print OUT "PROD_LIBS	+= Db\n";
	print OUT "PROD_LIBS	+= Com\n";
	print OUT "\n";

	if ($example)
	{
		print OUT "PROD += caExample\n";
	}
	else
	{
		print OUT "#PROD += xxx\n";
	}

	# OUT still set to src/Makefile.Host...
	print OUT "\n";
	print OUT "include \$(TOP)/config/RULES.Host\n";
	print OUT "#----------------------------------------\n";
	print OUT "#  ADD RULES AFTER THIS LINE\n";

	close OUT;

	open OUT, ">src/${app}Include.dbd"
		or die "Cannot open src/${app}Include.dbd";
	print OUT "include \"base.dbd\"\n";
	if ($example)
	{
		print OUT "include \"xxxRecord.dbd\"\n";
		print OUT "device(xxx,CONSTANT,devXxxSoft,\"SoftChannel\")\n";
	}
	else
	{
		print OUT "#private database definitions go here\n";
		print OUT "#device(xxx,CONSTANT,devXxxSoft,\"SoftChannel\")\n";
	}
	close OUT;


	if ( -f "${epics_base}/dbd/base.dbd" )
	{	copy ("${epics_base}/dbd/base.dbd", "src/base.dbd"); }
	else
	{	print "${epics_base}/dbd/base.dbd does not exist" }

	if ( -f "${epics_base}/dbd/baseLIBOBJS" )
	{	copy ("${epics_base}/dbd/baseLIBOBJS", "src/baseLIBOBJS"); }
	else
	{	print "${epics_base}/dbd/baseLIBOBJS does not exist" }


	if ($example)
	{
		open OUT, ">src/caExample.c" or
			die "Cannot open src/caExample.c";

		print OUT '
/* caExample.c */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cadef.h"

main (int argc,char **argv)
{
    double	data;
    int		status;
    chid	mychid;

    if(argc != 2)
    {
	fprintf(stderr, "usage: caExample pvname\n");
	exit(1);
    }
    SEVCHK( ca_task_initialize(),"ca_task_initialize");
    SEVCHK( ca_search(argv[1],&mychid),"ca_search failure");
    SEVCHK( ca_pend_io(5.0),"ca_pend_io failure");
    SEVCHK( ca_get(DBR_DOUBLE,mychid,(void *)&data),"ca_get failure");
    SEVCHK( ca_pend_io(5.0),"ca_pend_io failure");
    printf("%s %f\n",argv[1],data);
    return(0);
}

/*	EOF caExample.c */
';

		close OUT;

		open OUT, ">src/xxxRecord.c" or
			die "Cannot open src/xxxRecord.c";

		print OUT '
/* xxxRecord.c */
/* Example record support module */
  
#include <vxWorks.h>
#include <stddef.h>
#include <stdio.h>
#include <lstLib.h>
#include <string.h>

#include <alarm.h>
#include <dbEvent.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <devSup.h>
#include <errMdef.h>
#include <recSup.h>
#include <special.h>
#define GEN_SIZE_OFFSET
#include <xxxRecord.h>
#undef  GEN_SIZE_OFFSET

/* Create RSET - Record Support Entry Table*/
#define report NULL
#define initialize NULL
static long init_record();
static long process();
#define special NULL
#define get_value NULL
#define cvt_dbaddr NULL
#define get_array_info NULL
#define put_array_info NULL
static long get_units();
static long get_precision();
#define get_enum_str NULL
#define get_enum_strs NULL
#define put_enum_str NULL
static long get_graphic_double();
static long get_control_double();
static long get_alarm_double();
 
struct rset xxxRSET={
	RSETNUMBER,
	report,
	initialize,
	init_record,
	process,
	special,
	get_value,
	cvt_dbaddr,
	get_array_info,
	put_array_info,
	get_units,
	get_precision,
	get_enum_str,
	get_enum_strs,
	put_enum_str,
	get_graphic_double,
	get_control_double,
	get_alarm_double};

typedef struct xxxset { /* xxx input dset */
	long		number;
	DEVSUPFUN	dev_report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record; /*returns: (-1,0)=>(failure,success)*/
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	read_xxx;
}xxxdset;

static void alarm(xxxRecord *pxxx);
static void monitor(xxxRecord *pxxx);

static long init_record(void *precord,int pass)
{
    xxxRecord	*pxxx = (xxxRecord *)precord;
    xxxdset	*pdset;
    long	status;

    if (pass==0) return(0);

    if(!(pdset = (xxxdset *)(pxxx->dset))) {
	recGblRecordError(S_dev_noDSET,(void *)pxxx,"xxx: init_record");
	return(S_dev_noDSET);
    }
    /* must have read_xxx function defined */
    if( (pdset->number < 5) || (pdset->read_xxx == NULL) ) {
	recGblRecordError(S_dev_missingSup,(void *)pxxx,"xxx: init_record");
	return(S_dev_missingSup);
    }

    if( pdset->init_record ) {
	if((status=(*pdset->init_record)(pxxx))) return(status);
    }
    return(0);
}

static long process(void *precord)
{
    xxxRecord	    *pxxx = (xxxRecord *)precord;
    xxxdset	    *pdset = (xxxdset *)(pxxx->dset);
    long	     status;
    unsigned char    pact=pxxx->pact;

    if( (pdset==NULL) || (pdset->read_xxx==NULL) ) {
    	pxxx->pact=TRUE;
    	recGblRecordError(S_dev_missingSup,(void *)pxxx,"read_xxx");
    	return(S_dev_missingSup);
    }

    /* pact must not be set until after calling device support */
    status=(*pdset->read_xxx)(pxxx);
    /* check if device support set pact */
    if ( !pact && pxxx->pact ) return(0);
    pxxx->pact = TRUE;

    recGblGetTimeStamp(pxxx);
    /* check for alarms */
    alarm(pxxx);
    /* check event list */
    monitor(pxxx);
    /* process the forward scan link record */
    recGblFwdLink(pxxx);

    pxxx->pact=FALSE;
    return(status);
}

static long get_units(DBADDR *paddr, char *units)
{
    xxxRecord	*pxxx=(xxxRecord *)paddr->precord;

    strncpy(units,pxxx->egu,DB_UNITS_SIZE);
    return(0);
}

static long get_precision(DBADDR *paddr, long *precision)
{
    xxxRecord	*pxxx=(xxxRecord *)paddr->precord;

    *precision = pxxx->prec;
    if(paddr->pfield == (void *)&pxxx->val) return(0);
    recGblGetPrec(paddr,precision);
    return(0);
}

static long get_graphic_double(DBADDR *paddr,struct dbr_grDouble *pgd)
{
    xxxRecord	*pxxx=(xxxRecord *)paddr->precord;
    int		fieldIndex = dbGetFieldIndex(paddr);

    if(fieldIndex == xxxRecordVAL
    || fieldIndex == xxxRecordHIHI
    || fieldIndex == xxxRecordHIGH
    || fieldIndex == xxxRecordLOW
    || fieldIndex == xxxRecordLOLO
    || fieldIndex == xxxRecordHOPR
    || fieldIndex == xxxRecordLOPR) {
    	pgd->upper_disp_limit = pxxx->hopr;
    	pgd->lower_disp_limit = pxxx->lopr;
    } else recGblGetGraphicDouble(paddr,pgd);
    return(0);
}

static long get_control_double(DBADDR *paddr,struct dbr_ctrlDouble *pcd)
{
    xxxRecord	*pxxx=(xxxRecord *)paddr->precord;
    int		fieldIndex = dbGetFieldIndex(paddr);

    if(fieldIndex == xxxRecordVAL
    || fieldIndex == xxxRecordHIHI
    || fieldIndex == xxxRecordHIGH
    || fieldIndex == xxxRecordLOW
    || fieldIndex == xxxRecordLOLO) {
	pcd->upper_ctrl_limit = pxxx->hopr;
	pcd->lower_ctrl_limit = pxxx->lopr;
    } else recGblGetControlDouble(paddr,pcd);
    return(0);
}

static long get_alarm_double(DBADDR *paddr,struct dbr_alDouble *pad)
{
    xxxRecord	*pxxx=(xxxRecord *)paddr->precord;
    int		fieldIndex = dbGetFieldIndex(paddr);

    if(fieldIndex == xxxRecordVAL) {
	pad->upper_alarm_limit = pxxx->hihi;
	pad->upper_warning_limit = pxxx->high;
	pad->lower_warning_limit = pxxx->low;
	pad->lower_alarm_limit = pxxx->lolo;
    } else recGblGetAlarmDouble(paddr,pad);
    return(0);
}

static void alarm(xxxRecord *pxxx)
{
    double		val;
    float		hyst, lalm, hihi, high, low, lolo;
    unsigned short	hhsv, llsv, hsv, lsv;

    if(pxxx->udf == TRUE ){
    	recGblSetSevr(pxxx,UDF_ALARM,INVALID_ALARM);
    	return;
    }
    hihi = pxxx->hihi; lolo = pxxx->lolo; high = pxxx->high; low = pxxx->low;
    hhsv = pxxx->hhsv; llsv = pxxx->llsv; hsv = pxxx->hsv; lsv = pxxx->lsv;
    val = pxxx->val; hyst = pxxx->hyst; lalm = pxxx->lalm;

    /* alarm condition hihi */
    if (hhsv && (val >= hihi || ((lalm==hihi) && (val >= hihi-hyst)))){
    	if (recGblSetSevr(pxxx,HIHI_ALARM,pxxx->hhsv)) pxxx->lalm = hihi;
    	return;
    }

    /* alarm condition lolo */
    if (llsv && (val <= lolo || ((lalm==lolo) && (val <= lolo+hyst)))){
    	if (recGblSetSevr(pxxx,LOLO_ALARM,pxxx->llsv)) pxxx->lalm = lolo;
    	return;
    }

    /* alarm condition high */
    if (hsv && (val >= high || ((lalm==high) && (val >= high-hyst)))){
    	if (recGblSetSevr(pxxx,HIGH_ALARM,pxxx->hsv)) pxxx->lalm = high;
    	return;
    }

    /* alarm condition low */
    if (lsv && (val <= low || ((lalm==low) && (val <= low+hyst)))){
    	if (recGblSetSevr(pxxx,LOW_ALARM,pxxx->lsv)) pxxx->lalm = low;
    	return;
    }

    /* we get here only if val is out of alarm by at least hyst */
    pxxx->lalm = val;
    return;
}

static void monitor(xxxRecord *pxxx)
{
    unsigned short	monitor_mask;
    double		delta;

    monitor_mask = recGblResetAlarms(pxxx);
    /* check for value change */
    delta = pxxx->mlst - pxxx->val;
    if(delta<0.0) delta = -delta;
    if (delta > pxxx->mdel) {
    	/* post events for value change */
    	monitor_mask |= DBE_VALUE;
    	/* update last value monitored */
    	pxxx->mlst = pxxx->val;
    }

    /* check for archive change */
    delta = pxxx->alst - pxxx->val;
    if(delta<0.0) delta = -delta;
    if (delta > pxxx->adel) {
    	/* post events on value field for archive change */
    	monitor_mask |= DBE_LOG;
    	/* update last archive value monitored */
    	pxxx->alst = pxxx->val;
    }

    /* send out monitors connected to the value field */
    if (monitor_mask){
    	db_post_events(pxxx,&pxxx->val,monitor_mask);
    }
    return;
}
';	# END of xxxRecord.c

		close OUT;

		open OUT, ">src/xxxRecord.dbd" or
			die "Cannot open src/xxxRecord.dbd";

		print OUT '
recordtype(xxx) {
	include "dbCommon.dbd" 
	field(VAL,DBF_DOUBLE) {
		prompt("Current EGU Value")
		asl(ASL0)
		pp(TRUE)
	}
	field(INP,DBF_INLINK) {
		prompt("Input Specification")
		promptgroup(GUI_INPUTS)
		special(SPC_NOMOD)
		interest(1)
	}
	field(PREC,DBF_SHORT) {
		prompt("Display Precision")
		promptgroup(GUI_DISPLAY)
		interest(1)
	}
	field(EGU,DBF_STRING) {
		prompt("Engineering Units")
		promptgroup(GUI_DISPLAY)
		interest(1)
		size(16)
	}
	field(HOPR,DBF_FLOAT) {
		prompt("High Operating Range")
		promptgroup(GUI_DISPLAY)
		interest(1)
	}
	field(LOPR,DBF_FLOAT) {
		prompt("Low Operating Range")
		promptgroup(GUI_DISPLAY)
		interest(1)
	}
	field(HIHI,DBF_FLOAT) {
		prompt("Hihi Alarm Limit")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
	}
	field(LOLO,DBF_FLOAT) {
		prompt("Lolo Alarm Limit")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
	}
	field(HIGH,DBF_FLOAT) {
		prompt("High Alarm Limit")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
	}
	field(LOW,DBF_FLOAT) {
		prompt("Low Alarm Limit")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
	}
	field(HHSV,DBF_MENU) {
		prompt("Hihi Severity")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
		menu(menuAlarmSevr)
	}
	field(LLSV,DBF_MENU) {
		prompt("Lolo Severity")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
		menu(menuAlarmSevr)
	}
	field(HSV,DBF_MENU) {
		prompt("High Severity")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
		menu(menuAlarmSevr)
	}
	field(LSV,DBF_MENU) {
		prompt("Low Severity")
		promptgroup(GUI_ALARMS)
		pp(TRUE)
		interest(1)
		menu(menuAlarmSevr)
	}
	field(HYST,DBF_DOUBLE) {
		prompt("Alarm Deadband")
		promptgroup(GUI_ALARMS)
		interest(1)
	}
	field(ADEL,DBF_DOUBLE) {
		prompt("Archive Deadband")
		promptgroup(GUI_DISPLAY)
		interest(1)
	}
	field(MDEL,DBF_DOUBLE) {
		prompt("Monitor Deadband")
		promptgroup(GUI_DISPLAY)
		interest(1)
	}
	field(LALM,DBF_DOUBLE) {
		prompt("Last Value Alarmed")
		special(SPC_NOMOD)
		interest(3)
	}
	field(ALST,DBF_DOUBLE) {
		prompt("Last Value Archived")
		special(SPC_NOMOD)
		interest(3)
	}
	field(MLST,DBF_DOUBLE) {
		prompt("Last Val Monitored")
		special(SPC_NOMOD)
		interest(3)
	}
}
';	# END of xxxRecord.dbd

		open OUT, ">src/devXxxSoft.c" or
			die "Cannot open src/devXxxSoft.c";

		print OUT '
/* devXxxSoft.c */
/* Example device support module */

#include	<vxWorks.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>

#include	<alarm.h>
#include	<cvtTable.h>
#include	<dbDefs.h>
#include	<dbAccess.h>
#include        <recSup.h>
#include	<devSup.h>
#include	<link.h>
#include	<xxxRecord.h>

/*Create the dset for devXxxSoft */
static long init_record();
static long read_xxx();
struct {
    long		number;
    DEVSUPFUN	report;
    DEVSUPFUN	init;
    DEVSUPFUN	init_record;
    DEVSUPFUN	get_ioint_info;
    DEVSUPFUN	read_xxx;
}devXxxSoft={
    5,
    NULL,
    NULL,
    init_record,
    NULL,
    read_xxx,
};


static long init_record(pxxx)
    struct xxxRecord	*pxxx;
{
    if(recGblInitConstantLink(&pxxx->inp,DBF_DOUBLE,&pxxx->val))
    	 pxxx->udf = FALSE;
    return(0);
}

static long read_xxx(pxxx)
    struct xxxRecord	*pxxx;
{
    long status;

    status = dbGetLink(&(pxxx->inp),DBF_DOUBLE, &(pxxx->val),0,0);
    /*If return was succesful then set undefined false*/
    if(!status) pxxx->udf = FALSE;
    return(0);
}
';	# END of devXxxSoft.c

		open OUT, ">src/sncExample.st" or
			die "Cannot open src/sncExample.st";

		print OUT "program snctest\n";
		print OUT "float v;\n";
		print OUT "assign v to \"${user}:xxxExample\";\n";
		print OUT 'monitor v;
		
ss ss1
{
	state low
	{
		when(v>5.0)
		{
		printf("changing to high\n");
		} state high
		when(delay(.1)) 
		{
		} state low
	}
	state high
	{
		when(v<=5.0)
		{
		printf("changing to low\n");
		} state low
		when(delay(.1))
		{
		} state high
	}
}
';	# END of sncExample.st
	}

	chdir $startdir;

	unless (-d 'iocBoot')
	{
		mkdir iocBoot, 0777;
		open OUT, ">iocBoot/Makefile" or die "Cannot open iocBoot/Makefile";
		print OUT "TOP = ..\n";
		print OUT "include \$(TOP)/config/CONFIG_APP\n";
		print OUT "DIRS += \$(wildcard *ioc* )\n";
		print OUT "include \$(TOP)/config/RULES_DIRS\n";
		close OUT;
	}

	#	generate ioc stuff for this app?
	next if -d "iocBoot/ioc$app";

	print "What architecture do you want to use for your IOC?\n";
	print "e.g. pc486, mv167, ... >";
	$arch=<STDIN>;
	chomp $arch;

	mkdir "iocBoot/ioc$app", 0777;
	chdir "iocBoot/ioc$app" or die "Cannot create iocBoot/ioc$app";

	open OUT, ">Makefile" or die "Cannot open Makefile";
	print OUT "TOP = ../..\n";
	print OUT "include \$(TOP)/config/CONFIG_APP\n";
	print OUT "ARCH = $arch\n";
	print OUT "include \$(TOP)/config/RULES.ioc\n";
	close OUT;


	open OUT, ">st.cmd" or die "Cannot open st.cmd";
	print OUT "# Example vxWorks startup file\n";
	print OUT "#Following must be added for many board support packages\n";
	print OUT "#cd <full path to boot directory>\n";
	print OUT "#cd \"" . UNIXStyleName($startdir) . "\/iocBoot\/ioc$app" . "\"\n";
	print OUT "ld < bin/iocCore\n";
	print OUT "ld < bin/seq\n";
	print OUT "ld < bin/${app}Lib\n";
	print OUT "\n";
	print OUT "dbLoadDatabase(\"dbd/${app}App.dbd\")\n";
	if ($example)
	{
		print OUT
		  "dbLoadRecords(\"${app}App/Db/dbExample.db\",\"user=${user}\")\n";
		print OUT "iocInit\n";
		print OUT "seq &snctest\n";
	}
	else
	{
		print OUT "#dbLoadRecords(\"${app}App/Db/xxx.db\",\"user=${user}\")\n";
		print OUT "iocInit\n";
		print OUT "#start sequence programs\n";
		print OUT "#seq &sncxxx\n";
	}
	close OUT;

	chdir $startdir;
}



#	Cleanup (return-code [ messsage-line1, line 2, ... ])
sub Cleanup
{
	my ($rtncode, @message) = @_;

	chdir $startdir;
	print "Usage: $0 [-b EPICS base dir] [-e] {appname ...}\n\n"
				if ($rtncode != 0);

	foreach $line ( @message )
	{
		print "$line\n";
	}

	exit $rtncode;
}

sub GetHome # no args
{
	my ($home);

	# add to this list if new possibilities arise,
	# currently it's UNIX and WIN32:
	$home=$ENV{HOME} || $ENV{HOMEPATH};
	die "cannot locate home directory" unless $home;

	return $home;
}

sub UNIXStyleName
{
	my ($name) = @_;

	$name =~ s'^[a-z]:''i; # remove driver letter
	$name =~ s'\\'/'g;     # change slashed

	return $name;
}

sub GetUser # no args
{
	my ($user);

	# add to this list if new possibilities arise,
	# currently it's UNIX and WIN32:
	$user=$ENV{USER} || $ENV{USERNAME};

	unless ($user)
	{
		print "I cannot figure out your user name.\n";
		print "What shall you be called ?\n";
		print ">";
		$user = <STDIN>;
		chomp $user;
	}
	die "No user name" unless $user;

	return $user;
}

sub Wait # (seconds)
{
	my ($secs, $now, $i) = @_;

	$now=time;
	while (time() < $now+$secs)
	{
		$i=1234; # maybe the worst busy wait ever written
	}
}

