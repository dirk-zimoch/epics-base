#ifndef DBCHANNELNOOP_H
#define DBCHANNELNOOP_H

#include <string.h>
#include <string>

#include "cacIO.h"
#include "caerr.h"

/** @brief A channel which never connects
 *
 * Used when dbCa is placed in isolated mode for unittests
 */
class dbChannelNOOP : public cacChannel
{
    std::string myname;
public:
    dbChannelNOOP(const char *name, cacChannelNotify &notify)
        :cacChannel(notify)
        ,myname(name)
    {}

    virtual void destroy (
            CallbackGuard & ,
            epicsGuard < epicsMutex > & )
    {
        delete this; // goodbye cruel world
    }

    virtual unsigned getName (
            epicsGuard < epicsMutex > &,
            char * pBuf, unsigned bufLen ) const throw ()
    {
        const char* name = myname.c_str();
        if(bufLen>myname.size()+1) {
            bufLen=myname.size()+1;
        }
        memcpy(pBuf, name, bufLen);
        pBuf[--bufLen] = '\0';
        return bufLen;
    }

    // !! deprecated, avoid use  !!
    virtual const char * pName (
            epicsGuard < epicsMutex > & ) const throw ()
    {return myname.c_str();}

    virtual void show (
            epicsGuard < epicsMutex > &,
            unsigned level EPICS_UNUSED ) const
    {}

    virtual void initiateConnect (
            epicsGuard < epicsMutex > & )
    {}

    virtual unsigned requestMessageBytesPending (
            epicsGuard < epicsMutex > & )
    {return 0;}

    virtual void flush (
            epicsGuard < epicsMutex > & )
    {}

    virtual ioStatus read (
            epicsGuard < epicsMutex > &mut,
            unsigned type, arrayElementCount count,
            cacReadNotify &notify, ioid * = 0 )
    {
        notify.exception(mut, ECA_NORDACCESS, "dbChannelNOOP", type, count);
        return iosSynch;
    }

    virtual void write (
            epicsGuard < epicsMutex > &,
            unsigned type EPICS_UNUSED, arrayElementCount ,
            const void *pValue EPICS_UNUSED )
    {}

    virtual ioStatus write (
            epicsGuard < epicsMutex > &mut,
            unsigned type, arrayElementCount count,
            const void *pValue EPICS_UNUSED, cacWriteNotify & notify, ioid * = 0 )
    {
        notify.exception(mut, ECA_NOWTACCESS, "dbChannelNOOP", type, count);
        return iosSynch;
    }

    virtual void subscribe (
            epicsGuard < epicsMutex > &mut, unsigned type,
            arrayElementCount count, unsigned mask EPICS_UNUSED, cacStateNotify & notify,
            ioid * = 0 )
    {
        // should never subscribe
        notify.exception(mut, ECA_BADMASK, "dbChannelNOOP", type, count);
    }

    virtual void ioCancel (
            CallbackGuard & ,
            epicsGuard < epicsMutex > & ,
            const ioid & )
    {}

    virtual void ioShow (
            epicsGuard < epicsMutex > &,
            const ioid &, unsigned level EPICS_UNUSED ) const
    {}

    virtual short nativeType (
            epicsGuard < epicsMutex > & ) const
    {return 0;} // DBR_STRING

    virtual arrayElementCount nativeElementCount (
            epicsGuard < epicsMutex > & ) const
    {return 1;}
};

#endif // DBCHANNELNOOP_H
