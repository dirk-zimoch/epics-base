/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* SPDX-License-Identifier: EPICS
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/
/*
 * Implementation of utility macro substitution library (macLib)
 *
 * William Lupton, W. M. Keck Observatory
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbDefs.h"
#include "errlog.h"
#include "macLib.h"

/*
 * Parse macros definitions in "a=xxx,b=yyy" format and convert them to
 * a contiguously allocated array of pointers to names and values,
 * terminated with two NULL pointers, and the name and value strings.
 * Quotes and escapes are honored but only removed from macro names (not
 * values)
 */
long                            /* #defns encountered; <0 = ERROR */
epicsStdCall macParseDefns(
    MAC_HANDLE  *handle,        /* opaque handle; can be NULL if default */
                                /* special characters are to be used */

    const char  *defns,         /* macro definitions in "a=xxx,b=yyy" */
                                /* format */

    char        **pairs[] )     /* address of variable to receive pointer */
                                /* to NULL-terminated array of {name, */
                                /* value} pair strings; all storage is */
                                /* allocated contiguously */
{
    unsigned int numMax;
    unsigned int i;
    unsigned int num;
    int quote;
    int escape;
    size_t nbytes;
    size_t *start;
    int  *len;
    char *del;
    char *memCp, **memCpp;
    size_t c;
    char *s, *d, **p;
    enum { preName, inName, preValue, inValue } state;

    /* debug output */
    if ( handle && (handle->debug & 1) )
        printf( "macParseDefns( %s )\n", defns );

    /* allocate temporary index and length arrays; worst case they need to have
       two entries (name and value) for each comma plus one in the defns string */
    numMax = 2;
    for ( c = 0; defns[c] != '\0'; c++ ) if ( defns[c] == ',' ) numMax += 2;
    start = (size_t *) calloc( numMax, sizeof( size_t ) );
    len = (int *) calloc( numMax, sizeof( int ) );
    del = (char *) calloc( numMax, sizeof( char ) );
    if ( start == NULL || len == NULL  || del == NULL ) goto error;

    /* go through definitions, noting start index and length of macro
       names and values; honor quotes and escapes; ignore white space
       around assignment and separator characters */
    num    = 0;
    del[0] = FALSE;
    quote  = 0;
    state  = preName;
    for ( c = 0; defns[c] != '\0'; c++ ) {
        /* handle quotes */
        if ( quote )
            quote = ( defns[c] == quote ) ? 0 : quote;
        else if ( defns[c] == '\'' || defns[c] == '"' )
            quote = defns[c];

        /* handle escapes (index incremented below) */
        escape = ( defns[c] == '\\' && defns[c + 1] != '\0' );

        switch ( state ) {
          case preName:
            if ( !quote && !escape && ( isspace( (int) defns[c] ) || defns[c] == ',' ) ) break;
            start[num] = c;
            state = inName;
            /* fall through (may be empty name) */

          case inName:
            if ( quote || escape || ( defns[c] != '=' && defns[c] != ',' ) ) break;
            len[num] = c - start[num];
            while ( len[num] > 0 && isspace( (int) defns[start[num] + len[num] - 1] ) )
                len[num]--;
            num++;
            del[num] = FALSE;
            state = preValue;
            if ( defns[c] != ',' ) break;
            del[num] = TRUE;
            /* fall through (','; will delete) */

          case preValue:
            if ( !quote && !escape && isspace( (int) defns[c] ) ) break;
            start[num] = c;
            state = inValue;
            /* fall through (may be empty value) */

          case inValue:
            if ( quote || escape || defns[c] != ',' ) break;
            len[num] = c - start[num];
            while ( len[num] > 0 && isspace( (int) defns[start[num] + len[num] - 1] ) )
                len[num]--;
            num++;
            del[num] = FALSE;
            state = preName;
            break;
        }

        /* if this was escape, increment index now (couldn't do
           before because could have ignored escape at start of name
           or value) */
        if ( escape ) c++;
    }

    /* tidy up from state at end of string */
    switch ( state ) {
      case preName:
        break;
      case inName:
        len[num] = c - start[num];
        while ( len[num] > 0 && isspace( (int) defns[start[num] + len[num] - 1] ) )
            len[num]--;
        num++;
        del[num] = TRUE;
      case preValue:
        start[num] = c;
      case inValue:
        len[num] = c - start[num];
        while ( len[num] > 0 && isspace( (int) defns[start[num] + len[num] - 1] ) )
            len[num]--;
        num++;
        del[num] = FALSE;
    }

    /* debug output */
    if ( handle != NULL && handle->debug & 4 )
        for ( i = 0; i < num; i += 2 )
            printf( "[%d] %.*s = [%d] %.*s (%s) (%s)\n",
                    len[i+0], len[i+0], defns + start[i+0],
                    len[i+1], len[i+1], defns + start[i+1],
                    del[i+0] ? "del" : "nodel",
                    del[i+1] ? "del" : "nodel" );

    /* calculate how much memory to allocate: pointers followed by
       strings */
    nbytes = ( num + 2 ) * sizeof( char * );
    for ( i = 0; i < num; i++ )
        nbytes += len[i] + 1;

    /* allocate memory and set returned pairs pointer */
    memCp = malloc( nbytes );
    if ( memCp == NULL ) goto error;
    memCpp = ( char ** ) memCp;
    *pairs = memCpp;

    /* copy pointers and strings (memCpp accesses the pointer section
       and memCp accesses the string section) */
    memCp += ( num + 2 ) * sizeof( char * );
    for ( i = 0; i < num; i++ ) {

        /* if no '=' followed the name, macro will be deleted */
        if ( del[i] )
            *memCpp++ = NULL;
        else
            *memCpp++ = memCp;

        /* copy value regardless of the above */
        strncpy( memCp, defns + start[i], len[i] );
        memCp += len[i];
        *memCp++ = '\0';
    }

    /* add two NULL pointers */
    *memCpp++ = NULL;
    *memCpp++ = NULL;

    /* remove quotes and escapes from names in place (unlike values, they
       will not be re-parsed) */
    for ( p = *pairs; *p != NULL; p += 2 ) {
        quote = 0;
        for ( s = d = *p; *s != '\0'; s++ ) {

            /* quotes are not copied */
            if ( quote ) {
                if ( *s == quote ) {
                    quote = 0;
                    continue;
                }
            }
            else if ( *s == '\'' || *s == '"' ) {
                quote = *s;
                continue;
            }

            /* escapes are not copied but next character is */
            if ( *s == '\\' && *( s + 1 ) != '\0' )
                s++;

            /* others are copied */
            *d++ = *s;
        }

        /* need to terminate destination */
        *d++ = '\0';
    }

    /* free workspace */
    free( start );
    free( len );
    free( del );

    /* debug output */
    if ( handle != NULL && handle->debug & 1 )
        printf( "macParseDefns() -> %d\n", num / 2 );

    /* success exit; return number of definitions */
    return num / 2;

    /* error exit */
error:
    errlogPrintf( "macParseDefns: failed to allocate memory\n" );
    free( start );
    free( len );
    free( del );
    *pairs = NULL;
    return -1;
}

/*
 * Install an array of name / value pairs as macro definitions. The
 * array should have an even number of elements followed by at least
 * one (preferably two) NULL pointers
 */
long                            /* #macros defined; <0 = ERROR */
epicsStdCall macInstallMacros(
    MAC_HANDLE  *handle,        /* opaque handle */

    char        *pairs[] )      /* pointer to NULL-terminated array of */
                                /* {name,value} pair strings; a NULL */
                                /* value implies undefined; a NULL */
                                /* argument implies no macros */
{
    int n;
    char **p;

    /* debug output */
    if ( handle->debug & 1 )
        printf( "macInstallMacros( %s, %s, ... )\n",
                pairs && pairs[0] ? pairs[0] : "NULL",
                pairs && pairs[1] ? pairs[1] : "NULL" );

    /* go through array defining macros */
    for ( n = 0, p = pairs; p != NULL && p[0] != NULL; n++, p += 2 ) {
        if ( macPutValue( handle, p[0], p[1] ) < 0 )
            return -1;
    }

    /* debug output */
    if ( handle->debug & 1 )
        printf( "macInstallMacros() -> %d\n", n );

    /* return number of macros defined */
    return n;
}

