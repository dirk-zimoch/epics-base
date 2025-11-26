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
    static const size_t altNumMax = 4;
    size_t numMax;
    int i;
    int num;
    int quote;
    int escape;
    size_t nbytes;
    size_t *start;
    size_t *end;
    int *del;
    char *memCp, **memCpp;
    size_t c;
    char *s, *d, **p;
    enum { preName, inName, preValue, inValue } state;

    /* debug output */
    if ( handle && (handle->debug & 1) )
        printf( "macParseDefns( %s )\n", defns );

    /* allocate temporary index arrays; in worst case they need to have
       as many entries as the length of the defns string */
    numMax = strlen( defns );
    if ( numMax < altNumMax )
        numMax = altNumMax;
    start = (size_t *) calloc( numMax, sizeof( size_t ) );
    end = (size_t *) calloc( numMax, sizeof( size_t ) );
    del = (int *) calloc( numMax, sizeof( int ) );
    if ( start == NULL || end == NULL  || del == NULL ) goto error;

    /* go through definitions, noting indices of starts and ends of macro
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
            end[num] = c;
            while ( end[num] > start[num] && isspace( (int) defns[end[num] - 1] ) )
                end[num]--;
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
            end[num] = c;
            while ( end[num] > start[num] && isspace( (int) defns[end[num] - 1] ) )
                end[num]--;
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
        end[num] = c;
        while ( end[num] > start[num] && isspace( (int) defns[end[num] - 1] ) )
            end[num]--;
        num++;
        del[num] = TRUE;
      case preValue:
        start[num] = c;
      case inValue:
        end[num] = c;
        while ( end[num] > start[num] && isspace( (int) defns[end[num] - 1] ) )
            end[num]--;
        num++;
        del[num] = FALSE;
    }

    /* debug output */
    if ( handle != NULL && handle->debug & 4 )
        for ( i = 0; i < num; i += 2 )
            printf( "[%ld] %.*s = [%ld] %.*s (%s) (%s)\n",
                    (long) (end[i+0] - start[i+0]), (int) (end[i+0] - start[i+0]), defns + start[i+0],
                    (long) (end[i+1] - start[i+1]), (int) (end[i+1] - start[i+1]), defns + start[i+1],
                    del[i+0] ? "del" : "nodel",
                    del[i+1] ? "del" : "nodel" );

    /* calculate how much memory to allocate: pointers followed by
       strings */
    nbytes = ( num + 2 ) * sizeof( char * );
    for ( i = 0; i < num; i++ )
        nbytes += end[i] - start[i] + 1;

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
        strncpy( memCp, defns + start[i], end[i] - start[i] );
        memCp += end[i] - start[i];
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
    free( end );
    free( del );

    /* debug output */
    if ( handle != NULL && handle->debug & 1 )
        printf( "macParseDefns() -> %d\n", num / 2 );

    /* success exit; return number of definitions */
    return num / 2;

    /* error exit */
error:
    errlogPrintf( "macParseDefns: failed to allocate memory\n" );
    if ( start != NULL ) free( start );
    if ( end != NULL )   free( end );
    if ( del != NULL )   free( del );
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

