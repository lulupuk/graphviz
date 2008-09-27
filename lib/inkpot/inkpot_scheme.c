/* $Id$ $Revision$ */
/* vim:set shiftwidth=4 ts=8: */

/***********************************************************
 *      This software is part of the graphviz package      *
 *                http://www.graphviz.org/                 *
 *                                                         *
 *            Copyright (c) 1994-2008 AT&T Corp.           *
 *                and is licensed under the                *
 *            Common Public License, Version 1.0           *
 *                      by AT&T Corp.                      *
 *                                                         *
 *        Information and Software Systems Research        *
 *              AT&T Research, Florham Park NJ             *
 **********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "inkpot.h"
#include "inkpot_tables.h"
#include "inkpot_xlate.h"

static size_t inkpot_writer (void *closure, const char *data, size_t length)
{
    return fwrite(data, sizeof(char), length, (FILE *)closure);
}

static inkpot_disc_t inkpot_default_disc = { inkpot_writer, inkpot_writer };

static inkpot_status_t inkpot_clear ( inkpot_t *inkpot )
{
    inkpot->scheme_bits = 0;  /* clear schemes */
    inkpot->scheme_index = NULL;
    inkpot->out_name = NULL; 
    inkpot->out_scheme_index = NULL;

    return ((inkpot->status = INKPOT_SUCCESS));
}

inkpot_t *inkpot_init ( void )
{
    inkpot_status_t rc;
    inkpot_t *inkpot;
    IDX_MRU_CACHE i;
   
    inkpot = malloc(sizeof(inkpot_t));
    if (inkpot) {
	inkpot->canon = NULL;
	inkpot->canon_alloc = 0;

	inkpot->disc = inkpot_default_disc;
	inkpot->out_closure = stdout;
	inkpot->err_closure = stderr;

	inkpot->most_recently_used_idx = 0;
        for (i = 0; i < SZT_MRU_CACHE; i++)
	    inkpot->cache[i].next_recently_used_idx = i + 1;

	rc = inkpot_clear ( inkpot );
        assert ( rc == INKPOT_SUCCESS );
    }
    return inkpot;
}

void inkpot_destroy ( inkpot_t *inkpot )
{
    free(inkpot->canon);
    free(inkpot);
}

inkpot_status_t inkpot_disciplines ( inkpot_t *inkpot, inkpot_disc_t disc, void *out_closure, void *err_closure )
{
    inkpot->disc = disc;
    inkpot->out_closure = out_closure;
    inkpot->out_closure = out_closure;

    return ((inkpot->status = INKPOT_SUCCESS));
}

/* FIXME - this needs to work with UTF-8 strings */
static int string_cmpf (const char *k, const char *b)
{
    for ( ; *k && *b; k++, b++) {
	if (*k > *b) return 1;
	if (*k < *b) return -1;
    }
    if (*k) return 1;  /* if the next char in key is not '\0', then key is longer */
    if (*b) return -1;  /* if the next char in base is not '\0', then base is longer */
    return 0;
}

static int inkpot_scheme_name_cmpf ( const void *key, const void *base)
{
    const char *k = (const char*)key;
    const char *b = &TAB_STRINGS[((inkpot_scheme_name_t *)base)->string_idx];

    return string_cmpf(k, b);
}

static inkpot_scheme_name_t *inkpot_find_scheme_name ( const char *scheme )
{
    if (scheme == NULL)
        return NULL;
    return (inkpot_scheme_name_t *) bsearch(
            (void*)scheme, (void*)TAB_SCHEMES_NAME,
            SZT_SCHEMES_NAME, sizeof(inkpot_scheme_name_t),
            inkpot_scheme_name_cmpf); 
}

static int inkpot_scheme_index_cmpf ( const void *key, const void *base)
{
    const char *k = (const char*)key;
    const char *b = &TAB_STRINGS[((inkpot_scheme_index_t *)base)->string_idx];

    return string_cmpf(k, b);
}

static inkpot_scheme_index_t *inkpot_find_scheme_index ( const char *scheme )
{
    if (scheme == NULL)
        return NULL;
    return (inkpot_scheme_index_t *) bsearch(
            (void*)scheme, (void*)TAB_SCHEMES_INDEX,
            SZT_SCHEMES_INDEX, sizeof(inkpot_scheme_index_t),
            inkpot_scheme_index_cmpf); 
}

static inkpot_status_t inkpot_scheme ( inkpot_t *inkpot, const char *scheme )
{
    inkpot_scheme_name_t *inkpot_scheme_name;
    inkpot_scheme_index_t *inkpot_scheme_index;
    IDX_SCHEMES_NAME idx;

    if (scheme == NULL)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));

    inkpot_scheme_name = inkpot_find_scheme_name(scheme);
    if (inkpot_scheme_name) {
        idx = inkpot_scheme_name - TAB_SCHEMES_NAME;
        if (! inkpot->scheme_bits) {
            inkpot->default_scheme_name_idx = idx; /* first scheme is default */
            inkpot->default_value_idx = TAB_NAMES[inkpot_scheme_name->default_name_idx].value_idx;
        }
        inkpot->scheme_bits |= 1 << idx;
        return ((inkpot->status = INKPOT_SUCCESS));
    }

    inkpot_scheme_index = inkpot_find_scheme_index(scheme);
    if (! inkpot_scheme_index)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));
    if (inkpot->scheme_index != inkpot_scheme_index) {
        inkpot->scheme_index = inkpot_scheme_index;
	if (! inkpot->scheme_bits ) {
	    /* Set a default color from an index scheme only if no
	     * named schemes are currently active */
	    inkpot->default_value_idx = TAB_IXVALUES[inkpot_scheme_index->first_value_idx];
	}
    }
    return ((inkpot->status = INKPOT_SUCCESS));
}
 
inkpot_status_t inkpot_schemes ( inkpot_t *inkpot, const char *schemes )
{
    inkpot_status_t rc;
    static char *schemelist;
    static int schemelist_alloc;
    const char *q;
    char *p, *s, c;
    int len;

    if (schemes == NULL)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));

    len = strlen(schemes);
    if (len >= schemelist_alloc) {
	schemelist_alloc = len + 1 + 10;
	schemelist = realloc(schemelist, schemelist_alloc);
	if (! schemelist)
	    return ((inkpot->status = INKPOT_MALLOC_FAIL));
    }

    q = schemes;
    p = schemelist;
    rc = inkpot_clear(inkpot);
    for (c = *q; c;) {
        if (rc != INKPOT_SUCCESS)
	    return rc;
        s = p;
        while ((c = *q++) && c != ' ' && c != '\t' && c != ',') {*p++ = c;}
	*p++ = '\0';
	if (*s)
	    rc = inkpot_scheme(inkpot, s);
    }
    return rc;
}
 
inkpot_status_t inkpot_translate ( inkpot_t *inkpot, const char *scheme )
{
    inkpot_scheme_name_t *inkpot_scheme_name;
    inkpot_scheme_index_t *inkpot_scheme_index;
    IDX_SCHEMES_NAME idx;

    if (scheme == NULL)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));

    inkpot_scheme_name = inkpot_find_scheme_name(scheme);
    if (inkpot_scheme_name) {
        idx = inkpot_scheme_name - TAB_SCHEMES_NAME;
	if (! (inkpot->out_scheme_bit & (1 << idx))) {
        	inkpot->out_scheme_bit = 1 << idx;
		inkpot->out_scheme_index = NULL;
        	inkpot->out_name = NULL;     /* clear cached name */
	}
        return ((inkpot->status = INKPOT_SUCCESS));
    }

    inkpot_scheme_index = inkpot_find_scheme_index(scheme);
    if (! inkpot_scheme_index)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));
    if (inkpot->out_scheme_index != inkpot_scheme_index) {
	inkpot->out_scheme_index = inkpot_scheme_index;
       	inkpot->out_scheme_bit = 0;
	inkpot->out_name = NULL;     /* clear cached name */
    }
    return ((inkpot->status = INKPOT_SUCCESS));
}
 
static int inkpot_name_cmpf ( const void *key, const void *base)
{
    const char *k = (const char*)key;
    const char *b = &TAB_STRINGS[((inkpot_name_t *)base)->string_idx];

    return string_cmpf(k, b);
}

static inkpot_status_t inkpot_set_out_value( inkpot_t *inkpot, IDX_VALUES value_idx)
{
    if (inkpot->value_idx != value_idx) {
	inkpot->value_idx = value_idx;
	inkpot->out_name = NULL;  /* value changed so invalidate out_name */
    }
    return ((inkpot->status = INKPOT_SUCCESS));
}

static inkpot_status_t inkpot_cache_get( inkpot_t *inkpot )
{
    IDX_MRU_CACHE i;
    IDX_NAMES cache_name_idx;
    const char *cache_color;
    char *color;

    /* The cached value is valid if:
     *     The name schemes are unchanged
     *     The color requested matches the cached color.
     *
     * Its not sufficient for the scheme of the cached value to still be present in the current
     * schemes since the correct resolution may have the same name but different value.
     */
    color = inkpot->canon;
    for (i = 0; i < SZT_MRU_CACHE; i++) {  
	if (inkpot->cache[i].scheme_bits != inkpot->scheme_bits)
	    continue;

	cache_name_idx = inkpot->cache[i].name_idx;
	cache_color = &TAB_STRINGS[TAB_NAMES[cache_name_idx].string_idx];
	if (cache_color[0] != color[0] || (strcmp(cache_color, color) != 0))
	    continue;

	/* found */
	if (i) { /* if it is not already MRU then reorder to make it so */
	    inkpot->cache[i-1].next_recently_used_idx = inkpot->cache[i].next_recently_used_idx;
	    inkpot->cache[i].next_recently_used_idx = inkpot->most_recently_used_idx;
	    inkpot->most_recently_used_idx = i;
	}
        return inkpot_set_out_value(inkpot, TAB_NAMES[cache_name_idx].value_idx);
    }
    return ((inkpot->status = INKPOT_COLOR_UNKNOWN));
}

static inkpot_status_t inkpot_cache_put ( inkpot_t *inkpot, IDX_NAMES name_idx )
{
    IDX_MRU_CACHE i;

    for (i = 0; i < SZT_MRU_CACHE; i++) {  
	if (inkpot->cache[i].next_recently_used_idx == SZT_MRU_CACHE) {
	    inkpot->cache[i].next_recently_used_idx = inkpot->most_recently_used_idx;
	    inkpot->most_recently_used_idx = i;

	    inkpot->cache[i].scheme_bits = inkpot->scheme_bits;
	    inkpot->cache[i].name_idx = name_idx;
	}
	else
	    inkpot->cache[i].next_recently_used_idx += 1;
    }
    return ((inkpot->status = INKPOT_SUCCESS));
}

static inkpot_status_t inkpot_set_name ( inkpot_t *inkpot )
{
    inkpot_status_t rc;
    char *color;
    inkpot_name_t *name;
    IDX_NAME_ALTS i;
    IDX_NAMES base, top;

    if (inkpot == NULL || ! inkpot->scheme_bits)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));
    if ((color = inkpot->canon) == NULL)
        return ((inkpot->status = INKPOT_COLOR_UNKNOWN));

    rc = inkpot_cache_get(inkpot);
    if (rc == INKPOT_SUCCESS)
	return rc;

    for (i = 0; i < SZT_NAME_ALTS; ) {
	base = TAB_NAME_ALTS[i++];
	if (i == SZT_NAME_ALTS)
	    top = SZT_NAMES;
	else
	    top = TAB_NAME_ALTS[i];

        name = (inkpot_name_t *) bsearch(
            (void*)color, (void*)(&TAB_NAMES[base]),
	    top-base, sizeof(inkpot_name_t),
	    inkpot_name_cmpf); 
	if (name == NULL) 
            return ((inkpot->status = INKPOT_COLOR_UNKNOWN));
	
	if (inkpot->scheme_bits & name->scheme_bits) {
	    rc = inkpot_set_out_value(inkpot, name->value_idx);
	    assert(rc == INKPOT_SUCCESS);
	    return inkpot_cache_put(inkpot, (name - TAB_NAMES) );
	}
    }
    return ((inkpot->status = INKPOT_COLOR_UNKNOWN));
}

static inkpot_status_t inkpot_set_index ( inkpot_t *inkpot, int index )
{
    inkpot_scheme_index_t *scheme_index;
    IDX_SCHEMES_INDEX j;
    IDX_IXVALUES first, last;
    IDX_VALUES value_idx;

    scheme_index = inkpot->scheme_index;
    if (!scheme_index)
        return ((inkpot->status = INKPOT_SCHEME_UNKNOWN));

    first = scheme_index->first_value_idx;
    j = scheme_index - TAB_SCHEMES_INDEX;
    if (++j >= SZT_SCHEMES_INDEX)
	last = SZT_IXVALUES;
    else
	last = TAB_SCHEMES_INDEX[j].first_value_idx;
    last = last-1-first;

    /* clip user-provided index to fit available range */
    index = (index < 0)    ? 0    : index;
    index = (index > last) ? last : index;
    index += first;

    assert(index < SZT_IXVALUES);
    value_idx = TAB_IXVALUES[index];
    if (value_idx >= SZT_VALUES)
        assert(value_idx < SZT_VALUES + SZT_NONAME_VALUES);

    return inkpot_set_out_value(inkpot, value_idx);
}

inkpot_status_t inkpot_set_default( inkpot_t *inkpot )
{
    return inkpot_set_out_value(inkpot, inkpot->default_value_idx);
}

static int inkpot_rgba_cmpf ( const void *key, const void *base)
{
    RGBA rgba_key = *(RGBA*)key;
    RGBA rgba_base = *(RGBA*)base;

    if (rgba_key > rgba_base) return  1;
    if (rgba_key < rgba_base) return -1;
    return 0;
}

static inkpot_status_t inkpot_set_RGBA ( inkpot_t *inkpot, RGBA *rgba ) 
{
    inkpot_value_t *value;
    inkpot_noname_value_t *noname_value;

/*
 * FIXME - implement caching and check here
 */

    value = (inkpot_value_t *) bsearch(
        (void*)(rgba), (void*)TAB_VALUES,
        SZT_VALUES, sizeof(inkpot_value_t),
        inkpot_rgba_cmpf); 

    if (value)
	return inkpot_set_out_value(inkpot, (value - TAB_VALUES));

    noname_value = (inkpot_noname_value_t *) bsearch(
        (void*)(rgba), (void*)TAB_NONAME_VALUES,
        SZT_NONAME_VALUES, sizeof(inkpot_noname_value_t),
        inkpot_rgba_cmpf); 

    if (noname_value)
        return inkpot_set_out_value(inkpot, ((noname_value - TAB_NONAME_VALUES) + SZT_VALUES));

    return ((inkpot->status = INKPOT_COLOR_NONAME));
#if 0
    /* need some sort of btree here so that we can insert rgba
     * values and keep sorted */

    noname_value = (inkpot_noname_value_t *) bsearch(
        (void*)(rgba), (void*)TAB_DYNAMIC_VALUES,
        SZT_DYNAMIC_VALUES, sizeof(inkpot_noname_value_t),
        inkpot_rgba_cmpf); 
    
    /* insert value and keep sorted */

    return INKPOT_SUCCESS;

#endif
}

inkpot_status_t inkpot_set_rgba ( inkpot_t *inkpot, double rgba[4] )
{
    unsigned int myrgba = 0, v;
    int i;

    for (i = 0; i < 4; i++) {
	myrgba <<= SZB_RED;
	v = rgba[i];
	v = (v < 0.0) ? 0.0 : v;
	v = (v > 1.0) ? 1.0 : v;
	myrgba |= (int)(v * MAX_RED);
    }

    return inkpot_set_RGBA ( inkpot, &myrgba );
}

inkpot_status_t inkpot_set_hsva ( inkpot_t *inkpot, double hsva[4] )
{
    double rgba[4];

    hsva2rgba( hsva, rgba );

    return inkpot_set_rgba ( inkpot, rgba );
}

inkpot_status_t inkpot_set( inkpot_t *inkpot, const char *color )
{
    char *q;
    const char *p;
    int len, index;
    unsigned int rgba, c;
    double hsva[4];
    inkpot_status_t rc = INKPOT_COLOR_UNKNOWN;

    if (!color)
        return ((inkpot->status = INKPOT_COLOR_UNKNOWN));

    len = strlen(color);
    if (len >= inkpot->canon_alloc) {
	inkpot->canon_alloc = len + 1 + 20;
	inkpot->canon = realloc(inkpot->canon, inkpot->canon_alloc);
	if (! inkpot->canon)
            return ((inkpot->status = INKPOT_MALLOC_FAIL));
    }

    /* canonicalize input string */
    for (p = color, q = inkpot->canon;
		(c = *p) && ( c == ' ' || c == '\t' );
		p++) { }; /* remove leading ws */

    /* change ',' to ' ' */
    /* FIXME - is this UTF-8 safe ? */
    while ((c = *p++)) {
	if (c == ',')
	    c = ' ';
	*q++ = c;
    }
    *q = '\0';

    if (*inkpot->canon == '#') {
        if (sscanf(inkpot->canon, "#%8x", &rgba))
	    rc = inkpot_set_RGBA(inkpot, &rgba);

        if (rc != INKPOT_SUCCESS) {
            if (sscanf(inkpot->canon, "#%6x", &rgba)) {
	        rgba = (rgba << SZB_RED) | MAX_RED;
	        rc = inkpot_set_RGBA(inkpot, &rgba);
            }
   	} 
    }

    if ((rc != INKPOT_SUCCESS) || ((c = *inkpot->canon) == '.') || isdigit(c)) {
	hsva[3] = 1.0;
        if (sscanf(inkpot->canon, "%lf%lf%lf%lf", &hsva[0], &hsva[1], &hsva[2], &hsva[3]) >= 3)
	    rc = inkpot_set_hsva(inkpot, hsva);
    }

    if (rc != INKPOT_SUCCESS)
        if (sscanf(inkpot->canon, "%d", &index) == 1)
            rc = inkpot_set_index(inkpot, index);

    if (rc != INKPOT_SUCCESS) {
	/* remove embedded ws and convert to lower case*/
        for (p = q = inkpot->canon;
		(c = *p) && ! ( c == ' ' || c == '\t' ); p++) {
	    *q++ = tolower(c);
	};
	*q = '\0';
        rc = inkpot_set_name(inkpot);
    }

    return rc;
}

inkpot_status_t inkpot_get ( inkpot_t *inkpot, const char **color )
{
    inkpot_name_t *out_name;
    IDX_NAMES t;
    IDX_VALUES value_idx;
    
    out_name = inkpot->out_name;
    if (out_name) {  /* if we have a cached name */
    	*color = &TAB_STRINGS[out_name->string_idx];
        return ((inkpot->status = INKPOT_SUCCESS));
    }
    
    if (inkpot->out_scheme_bit) {
        value_idx = inkpot->value_idx;
        if (value_idx < SZT_VALUES) {
            for (t = TAB_VALUES[value_idx].toname_idx; t < SZT_NAMES; t++) {
                out_name = &TAB_NAMES[TAB_NAMES[t].toname_idx];
                if (out_name->value_idx != value_idx)
                    break;
                if (out_name->scheme_bits & inkpot->out_scheme_bit) {
	            inkpot->out_name = out_name;
                    *color = &TAB_STRINGS[out_name->string_idx];
		    return ((inkpot->status = INKPOT_SUCCESS));
                 }
            }
            *color = NULL;
	    return ((inkpot->status = INKPOT_COLOR_NONAME));
        }
        value_idx -= SZT_VALUES;
        if (value_idx < SZT_NONAME_VALUES) {
            *color = NULL;
	    return ((inkpot->status = INKPOT_COLOR_NONAME));
        }
        assert(0);  /* support for dynamic values to go here */
    }

    if (inkpot->out_scheme_index) {
	*color = NULL;
	return ((inkpot->status = INKPOT_COLOR_NONAME));
    }

    *color = NULL;
    return ((inkpot->status = INKPOT_COLOR_NONAME));
}

static inkpot_status_t inkpot_get_RGBA ( inkpot_t *inkpot, RGBA *rgba )
{
    IDX_VALUES value_idx = inkpot->value_idx;

    if (value_idx < SZT_VALUES)
	*rgba = TAB_VALUES[value_idx].rgba;
    else {
	assert (value_idx < SZT_VALUES + SZT_NONAME_VALUES);
	*rgba = TAB_NONAME_VALUES[value_idx - SZT_VALUES].rgba;
    }

    return ((inkpot->status = INKPOT_SUCCESS));
}

inkpot_status_t inkpot_get_rgba_i ( inkpot_t *inkpot, unsigned int rgba[4] )
{
    inkpot_status_t rc;
    RGBA myrgba;

    rc = inkpot_get_RGBA( inkpot, &myrgba );
    if (rc == INKPOT_SUCCESS) {
        rgba[3] = myrgba & MSK_RED; myrgba >>= SZB_RED;
        rgba[2] = myrgba & MSK_RED; myrgba >>= SZB_RED;
        rgba[1] = myrgba & MSK_RED; myrgba >>= SZB_RED;
        rgba[0] = myrgba & MSK_RED;
    }
    return rc;
}

inkpot_status_t inkpot_get_rgba ( inkpot_t *inkpot, double rgba[4] )
{
    inkpot_status_t rc;
    RGBA myrgba;
    int i;

    rc = inkpot_get_RGBA( inkpot, &myrgba );
    if (rc == INKPOT_SUCCESS) {
	for (i = 3; i >= 0; i--) {
	    rgba[i] = (myrgba & MSK_RED) / (double)MAX_RED;
	    myrgba >>= SZB_RED;
	}
    }
    return rc;
}

inkpot_status_t inkpot_get_hsva ( inkpot_t *inkpot, double hsva[4] )
{
    inkpot_status_t rc;
    double rgba[4];

    rc = inkpot_get_rgba(inkpot, rgba);
    if (rc == INKPOT_SUCCESS)
	rgba2hsva( rgba, hsva );
 
    return rc;
}

inkpot_status_t inkpot_get_cmyk ( inkpot_t *inkpot, double cmyk[4] )
{
    inkpot_status_t rc;
    double rgba[4];

    rc = inkpot_get_rgba(inkpot, rgba);
    if (rc == INKPOT_SUCCESS)
	rgba2cmyk( rgba, cmyk );
 
    return rc;
}

#if 0
/* FIXME - requires palette collection and transformation */
inkpot_status_t inkpot_get_index ( inkpot_t *inkpot, unsigned int *index )
{
    /* FIXME */
    return ((inkpot->status = INKPOT_FAIL));
}
#endif

static void errputs(inkpot_t *inkpot, const char *s)
{
    inkpot->disc.err_writer(inkpot->err_closure, s, strlen(s));
}

inkpot_status_t inkpot_debug_schemes( inkpot_t *inkpot )
{
    IDX_SCHEMES_NAME i;
    int found;

    errputs(inkpot, "schemes:\n");
    for (i = 0; i < SZT_SCHEMES_NAME; i++) {
	found = 0;
        if ((1<<i) & inkpot->scheme_bits) {
            errputs(inkpot, &TAB_STRINGS[TAB_SCHEMES_NAME[i].string_idx]);
            errputs(inkpot, " (in)");
            if (i == inkpot->default_scheme_name_idx) 
                errputs(inkpot, " (default)");
	    found++;
        }
        if ((1<<i) & inkpot->out_scheme_bit) {
	    if (! found)
                errputs(inkpot, &TAB_STRINGS[TAB_SCHEMES_NAME[i].string_idx]);
            errputs(inkpot, " (out)");
	    found++;
        }
	if (found)
            errputs(inkpot, "\n");
    }
    found = 0;
    if (inkpot->scheme_index) {
        errputs(inkpot, &TAB_STRINGS[inkpot->scheme_index->string_idx]);
        errputs(inkpot, " (indexed) (in)");
	found++;
    }
    if (inkpot->out_scheme_index) {
	if (! found) {
            errputs(inkpot, &TAB_STRINGS[inkpot->out_scheme_index->string_idx]);
            errputs(inkpot, " (indexed)");
	}
        errputs(inkpot, " (out)");
	found++;
    }
    if (found)
        errputs(inkpot, "\n");
    errputs(inkpot, "\n");

    return ((inkpot->status = INKPOT_SUCCESS));
}

static inkpot_status_t inkpot_debug_scheme_names( inkpot_t *inkpot, int scheme_bits )
{
    IDX_SCHEMES_NAME i;
    int found = 0;

    errputs(inkpot, "(");
    for (i = 0; i < SZT_SCHEMES_NAME; i++) {
        if ((1 << i) & scheme_bits) {
            if (found++)
                errputs(inkpot, " ");
            errputs(inkpot, &TAB_STRINGS[TAB_SCHEMES_NAME[i].string_idx]);
        }
    }
    errputs(inkpot, ")");

    return INKPOT_SUCCESS;
}

static void inkpot_debug_rgba( inkpot_t *inkpot, RGBA rgba )
{
    char buf[20];
    unsigned int r, g, b, a;

    a = rgba & MSK_RED; rgba >>= SZB_RED;
    b = rgba & MSK_RED; rgba >>= SZB_RED;
    g = rgba & MSK_RED; rgba >>= SZB_RED;
    r = rgba & MSK_RED;

    sprintf(buf, "%d,%d,%d,%d", r, g, b, a);
    errputs(inkpot, buf);
}

static inkpot_status_t inkpot_debug_names_schemes( inkpot_t *inkpot, MSK_SCHEMES_NAME scheme_bits, inkpot_scheme_index_t *scheme_index )
{
    inkpot_name_t *name;
    IDX_NAMES i;
    IDX_SCHEMES_INDEX j;
    IDX_IXVALUES k, first, last;
    IDX_VALUES v;
    char buf[20];

    if (scheme_bits) {
        for (i = 0; i < SZT_NAMES; i++) {
            name = &TAB_NAMES[i];
            if (scheme_bits & name->scheme_bits) {
                errputs(inkpot, &TAB_STRINGS[TAB_NAMES[i].string_idx]);
		inkpot_debug_scheme_names(inkpot, scheme_bits);
		errputs(inkpot, " ");
		inkpot_debug_rgba(inkpot, TAB_VALUES[name->value_idx].rgba);
		if (name->value_idx == inkpot->default_value_idx)
		    errputs(inkpot, " (default)");
		errputs(inkpot, "\n");
            }
        }
    }
    if (scheme_index) {
	first = scheme_index->first_value_idx;
	j = scheme_index - TAB_SCHEMES_INDEX;
	if (++j >= SZT_SCHEMES_INDEX)
	    last = SZT_IXVALUES;
	else
	    last = TAB_SCHEMES_INDEX[j].first_value_idx;

	for (k = first; k < last; k++) {
	    v = TAB_IXVALUES[k];
	    sprintf(buf, "%d(", k - first);
	    errputs(inkpot, buf);
	    errputs(inkpot, &TAB_STRINGS[scheme_index->string_idx]);
	    errputs(inkpot, ") ");
	    if (v < SZT_VALUES)
	        inkpot_debug_rgba(inkpot, TAB_VALUES[v].rgba);
	    else
	        inkpot_debug_rgba(inkpot, TAB_NONAME_VALUES[v - SZT_VALUES].rgba);
	    errputs(inkpot, "\n");
	}
    }
    errputs(inkpot, "\n");

    return ((inkpot->status = INKPOT_SUCCESS));
}

inkpot_status_t inkpot_debug_names( inkpot_t *inkpot )
{
    MSK_SCHEMES_NAME scheme_bits = inkpot->scheme_bits;
    inkpot_scheme_index_t *scheme_index = inkpot->scheme_index;

    errputs(inkpot, "names (in):\n");
    return inkpot_debug_names_schemes(inkpot, scheme_bits, scheme_index);
}

inkpot_status_t inkpot_debug_out_names( inkpot_t *inkpot )
{
    MSK_SCHEMES_NAME scheme_bits = inkpot->out_scheme_bit;
    inkpot_scheme_index_t *scheme_index = inkpot->out_scheme_index;

    errputs(inkpot, "names (out):\n");
    return inkpot_debug_names_schemes(inkpot, scheme_bits, scheme_index);
}

/* Print all values that are members of the currently listed
 * name schemes, with the names in those schemes.
 * Does not print the indexes in index schemes that a value may
 * be a member of. */
inkpot_status_t inkpot_debug_values( inkpot_t *inkpot )
{
    inkpot_value_t *value;
    inkpot_name_t *name;
    IDX_VALUES i;
    IDX_NAMES t;
    MSK_SCHEMES_NAME scheme_bits;
    int found;

    errputs(inkpot, "values:\n");
    for (i = 0; i < SZT_VALUES; i++) {
        value = &TAB_VALUES[i];
        found = 0;
        for (t = value->toname_idx; t < SZT_NAMES; t++) {
            name = &TAB_NAMES[TAB_NAMES[t].toname_idx];
            if (name->value_idx != i)
                break;
            scheme_bits = name->scheme_bits & inkpot->scheme_bits;
            if (scheme_bits) {
                if (found++)
                    errputs(inkpot, " ");
                else
		    inkpot_debug_rgba(inkpot, TAB_VALUES[i].rgba);
                errputs(inkpot, " ");
                errputs(inkpot, &TAB_STRINGS[name->string_idx]);
		inkpot_debug_scheme_names(inkpot, scheme_bits);
            }
        }
        if (found)
            errputs(inkpot, "\n");
    }
    errputs(inkpot, "\n");

    return ((inkpot->status = INKPOT_SUCCESS));
}

inkpot_status_t inkpot_write ( inkpot_t *inkpot )
{
    inkpot_status_t rc;
    const char *color;
    IDX_VALUES value_idx;
    RGBA rgba;
    char buf[10];

    rc = inkpot_get(inkpot, &color);
    if (rc == INKPOT_SUCCESS)
	inkpot->disc.out_writer(inkpot->out_closure, color, strlen(color));
    if (rc == INKPOT_COLOR_NONAME) {
        value_idx = inkpot->value_idx;
        if (value_idx < SZT_VALUES)
	    rgba = TAB_VALUES[value_idx].rgba;
        else {
	    assert (value_idx < SZT_VALUES + SZT_NONAME_VALUES);
	    rgba = TAB_NONAME_VALUES[value_idx - SZT_VALUES].rgba;
        }

	sprintf(buf, "#%08x", rgba);
	inkpot->disc.out_writer(inkpot->out_closure, buf, sizeof(buf));
    }
    return rc;
}

inkpot_status_t inkpot_error ( inkpot_t *inkpot )
{
    const char *m;

    assert(inkpot);
    switch (inkpot->status) {
	case INKPOT_SUCCESS:
	    m = "INKPOT_SUCCESS\n"; break;
	case INKPOT_MALLOC_FAIL:
	    m = "INKPOT_MALLOC_FAIL\n"; break;
	case INKPOT_COLOR_UNKNOWN:
	    m = "INKPOT_COLOR_UNKNOWN\n"; break;
	case INKPOT_COLOR_NONAME:
	    m = "INKPOT_COLOR_NONAME\n"; break;
	case INKPOT_SCHEME_UNKNOWN:
	    m = "INKPOT_SCHEME_UNKNOWN\n"; break;
    }
    inkpot->disc.err_writer(inkpot->err_closure, m, strlen(m));

    return ((inkpot->status = INKPOT_SUCCESS));
};
