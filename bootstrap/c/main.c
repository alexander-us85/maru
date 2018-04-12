#include <stdio.h>
#include <locale.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>

#include "ffi.h"


extern int isatty(int);

#if defined(_MSC_VER)
    #if defined(WIN32)
        #include <malloc.h>
        #define swnprintf(BUF, SIZE, FMT, ARG) swprintf(BUF, FMT, ARG)
    #else
        #define swnprintf swprintf
    #endif
    #include <basetsd.h>
    typedef SSIZE_T ssize_t;
#endif

// #define	TAG_INT	1
// #define	LIB_GC	1

#if defined(NDEBUG)
    #define GC_APP_HEADER int type;
#else
    #define GC_APP_HEADER	int printing, type;
#endif

#define GC_SAVE		1

#if (LIB_GC)
    #include "libgc.c"
#else
    #include "gc.c"
#endif
#include "wcs.c"
#include "buffer.c"

union Object;

typedef union Object *oop;

typedef oop (*imp_t)(oop args, oop env);

typedef union {
    int		 arg_int;
    int32_t	 arg_int32;
    int64_t	 arg_int64;
    long	 arg_long;
    float	 arg_float;
    double	 arg_double;
    void	*arg_pointer;
    char	*arg_string;
    wchar_t	*arg_String;
} arg_t;

typedef void (*cast_t)(oop arg, void **argp, arg_t *buf);

typedef struct {
    int		 arg_count;
    int		 arg_rest;
    ffi_type	*arg_types[32];
    cast_t	 arg_casts[32];
} proto_t;

#define nil ((oop)0)

enum {
    Undefined, Data, Long, Double, String, Symbol, Pair, _Array, Array, Expr, Form, Fixed, Subr,
};

typedef int64_t long_t;

#define LD	"lld"

struct Data     { int array[0]; };
struct Long	    { long_t    bits; };
struct Double	{ double    bits; };
struct String	{ oop	    size;  wchar_t *bits; };	/* bits is in managed memory */
struct Symbol	{ wchar_t  *bits; int flags; };
struct Pair	    { oop	    head, tail, source; };
struct Array	{ oop	    size, _array; };
struct Expr	    { oop	    name, definition, environment, profile; };
struct Form	    { oop	    function, symbol; };
struct Fixed	{ oop	    function; };
struct Subr	    { wchar_t  *name;  imp_t imp;  proto_t *sig;  int profile; };

union Object {
    struct Data		Data;
    struct Long		Long;
    struct Double	Double;
    struct String	String;
    struct Symbol	Symbol;
    struct Pair		Pair;
    struct Array	Array;
    struct Expr		Expr;
    struct Form		Form;
    struct Fixed	Fixed;
    struct Subr		Subr;
};

static void fatal(char *reason, ...);

#if !defined(NDEBUG)
    #define setType(OBJ, TYPE) ptr2hdr(OBJ)->printing= 0; (ptr2hdr(OBJ)->type= (TYPE))
#else                          
    #define setType(OBJ, TYPE) (ptr2hdr(OBJ)->type= (TYPE))
#endif

#if (TAG_INT)
    static inline int getType(oop obj)	{ 
        return obj ? (((long)obj & 1) ? Long : ptr2hdr(obj)->type) : Undefined; 
    }
#else
    static inline int getType(oop obj)	{ 
        return obj ? ptr2hdr(obj)->type : Undefined; 
    }
#endif

#define is(TYPE, OBJ) ((OBJ) && (TYPE == getType(OBJ)))

#if defined(NDEBUG)
    #define checkType(OBJ, TYPE) OBJ
#else
    #define checkType(OBJ, TYPE) _checkType(OBJ, TYPE, #TYPE, __FILE__, __LINE__)
    static inline oop _checkType(oop obj, int type, char *name, char *file, int line)
    {
        if (obj && !((long)obj & 1) && !ptr2hdr(obj)->used) {
            fatal("%s:%i: attempt to access dead object %s\n", file, line, name);
        }
        if (!is(type, obj)) {
            fatal("%s:%i: typecheck failed for %s (%i != %i)\n", file, line, name, type, getType(obj));
        }
	    return obj;
    }
#endif

#define get(OBJ, TYPE, FIELD)		    (checkType(OBJ, TYPE)->TYPE.FIELD)
#define set(OBJ, TYPE, FIELD, VALUE)	(checkType(OBJ, TYPE)->TYPE.FIELD= (VALUE))

#define getHead(OBJ)	                get(OBJ, Pair,head)
#define getTail(OBJ)	                get(OBJ, Pair,tail)

#define setHead(OBJ, VAL)	            set(OBJ, Pair,head, VAL)
#define setTail(OBJ, VAL)	            set(OBJ, Pair,tail, VAL)

static oop car(oop obj)			        { return is(Pair, obj) ? getHead(obj) : nil; }
static oop cdr(oop obj)			        { return is(Pair, obj) ? getTail(obj) : nil; }
                                        
static oop caar(oop obj)		        { return car(car(obj)); }
static oop cadr(oop obj)		        { return car(cdr(obj)); }
static oop cdar(oop obj)		        { return cdr(car(obj)); }
static oop cddr(oop obj)		        { return cdr(cdr(obj)); }
static oop caaar(oop obj)		        { return car(car(car(obj))); }
static oop cadar(oop obj)		        { return car(cdr(car(obj))); }
static oop caddr(oop obj)		        { return car(cdr(cdr(obj))); }
static oop cadddr(oop obj)		        { return car(cdr(cdr(cdr(obj)))); }
                                        
#define getVar(X)			            getTail(X)
#define setVar(X, V)			        setTail(X, V)

static oop _newBits(int type, size_t size)	{ oop obj= GC_malloc_atomic(size); setType(obj, type);  return obj; }
static oop _newOops(int type, size_t size)	{ oop obj= GC_malloc(size);		   setType(obj, type);  return obj; }

#define newBits(TYPE) _newBits(TYPE, sizeof(struct TYPE))
#define newOops(TYPE) _newOops(TYPE, sizeof(struct TYPE))

static char *argv0;

static oop symbols            = nil, 
           globals            = nil, 
           globalNamespace    = nil, 
           expanders          = nil, 
           encoders           = nil, 
           evaluators         = nil, 
           applicators        = nil, 
           backtrace          = nil, 
           arguments          = nil, 
           input              = nil, 
           output             = nil;
static int traceDepth         = 0;
static oop traceStack         = nil, 
           currentPath        = nil, 
           currentLine        = nil, 
           currentSource      = nil;
static oop s_locals           = nil, 
           s_set              = nil, 
           s_define           = nil, 
           s_let              = nil, 
           s_lambda           = nil, 
           s_quote            = nil, 
           s_quasiquote       = nil, 
           s_unquote          = nil, 
           s_unquote_splicing = nil, 
           s_t                = nil, 
           s_dot              = nil, 
           s_etc              = nil, 
           s_bracket          = nil, 
           s_brace            = nil, 
           s_main             = nil;

static int opt_b = 0, opt_g = 0, opt_O = 0, opt_p = 0, opt_v = 0;

static oop newData(size_t len)		{ return _newBits(Data, len); }

#if (TAG_INT)
    static inline int isLong(oop x) { 
        return (((long)x & 1) || Long == getType(x)); 
    }

    static inline oop newLong(long_t x) { 
        if ((x ^ (x << 1)) < 0) { 
            oop obj = newBits(Long);  
            set(obj, Long,bits, x);  
            return obj; 
        }  
        return ((oop)((x << 1) | 1)); 
    }
    static inline long_t getLong(oop x)	{ 
        if ((long)x & 1) return (long)x >> 1;	 
        return get(x, Long,bits); 
    }
#else
    #define isLong(X) is(Long, (X))
    static oop newLong(long bits) { 
        oop obj = newBits(Long);  
        set(obj, Long,bits, bits);  
        return obj; 
    }
    #define getLong(X) get((X), Long,bits)
#endif

static void setDouble(oop obj, double bits)	{
    memcpy(&obj->Double.bits, &bits, sizeof(bits)); 
}

static double getDouble(oop obj) {
    double bits;	
    memcpy(&bits, &obj->Double.bits, sizeof(bits));	 
    return bits; 
}

#define isDouble(X)	is(Double, (X))
#define isPair(X)	is(Pair, (X))

static inline int isNumeric(oop obj) { return isLong(obj) || isDouble(obj); }

static oop newDouble(double bits) { 
    oop obj = newBits(Double);  
    setDouble(obj, bits);  
    return obj; 
}

static oop _newString(size_t len)
{
    wchar_t *gstr = (wchar_t *)_newBits(-1, sizeof(wchar_t) * (len + 1)); /* + 1 to ensure null terminator */
    GC_PROTECT(gstr);	
    oop obj = newOops(String);			 GC_PROTECT(obj);
    set(obj, String,size, newLong(len)); GC_UNPROTECT(obj);
    set(obj, String,bits, gstr);		 GC_UNPROTECT(gstr);
    return obj;
}

static oop newStringN(wchar_t *cstr, size_t len)
{
    oop obj = _newString(len);
    memcpy(get(obj, String,bits), cstr, sizeof(wchar_t) * len);
    return obj;
}

static oop newString(wchar_t *cstr)
{
    return newStringN(cstr, wcslen(cstr));
}

static int stringLength(oop string)
{
    return getLong(get(string, String,size));
}

static oop newSymbol(wchar_t *cstr)	{ 
    oop obj= newBits(Symbol);	
    set(obj, Symbol,bits, wcsdup(cstr));			
    return obj; 
}

static int symbolLength(oop symbol)
{
    return wcslen(get(symbol, Symbol,bits));
}

static oop cons(oop head, oop tail)	{ 
    oop obj = newOops(Pair);	
    set(obj, Pair,head, head);  
    set(obj, Pair,tail, tail);	
    return obj; 
}

static oop newPairFrom(oop head, oop tail, oop source)
{
    oop obj = newOops(Pair);
    set(obj, Pair,head,	head);
    set(obj, Pair,tail,	tail);
    set(obj, Pair,source, get(source, Pair,source));
    return obj;
}

static oop newArray(int size)
{
    int cap  = size ? size : 1;
    oop elts = _newOops(_Array, sizeof(oop) * cap);	GC_PROTECT(elts);
    oop obj  = newOops( Array);				        GC_PROTECT(obj);
    set(obj, Array,_array, elts);
    set(obj, Array,size, newLong(size));		    GC_UNPROTECT(obj);  
    GC_UNPROTECT(elts);
    return obj;
}

static int arrayLength(oop obj)
{
    return is(Array, obj) ? getLong(get(obj, Array,size)) : 0;
}

static oop arrayAt(oop array, int index)
{
    if (is(Array, array)) {
	    oop elts = get(array, Array,_array);
	    int size = arrayLength(array);
	    if ((unsigned)index < (unsigned)size)
	        return ((oop *)elts)[index];
    }
    return nil;
}

static oop arrayAtPut(oop array, int index, oop val)
{
    if (is(Array, array)) {
	    int size = arrayLength(array);
	    oop elts = get(array, Array,_array);
	    if ((unsigned)index >= (unsigned)size) {
	        GC_PROTECT(array);
	        int cap = GC_size(elts) / sizeof(oop);
	        if (index >= cap) {
		        while (cap <= index) cap *= 2;
		        oop oops = _newOops(_Array, sizeof(oop) * cap);
		        memcpy((oop *)oops, (oop *)elts, sizeof(oop) * size);
		        elts = set(array, Array,_array, oops);
	        }
	        set(array, Array,size, newLong(index + 1));
	        GC_UNPROTECT(array);
	    }
	    return ((oop *)elts)[index]= val;
    }
    return nil;
}

static oop arrayAppend(oop array, oop val)
{
    return arrayAtPut(array, arrayLength(array), val);
}

static oop arrayInsert(oop obj, size_t index, oop value)
{
    size_t len = arrayLength(obj);
    arrayAppend(obj, value);
    if (index < len) {
	    oop  elts = get(obj, Array,_array);
	    oop *oops = (oop *)elts + index;
	    memmove(oops + 1, oops, sizeof(oop) * (len - index));
    }
    arrayAtPut(obj, index, value);
    return value;
}

static oop oopAt(oop obj, int index)
{
    if (obj && !isLong(obj) && !GC_atomic(obj)) {
	    int size = GC_size(obj) / sizeof(oop);
	    if ((unsigned)index < (unsigned)size) 
            return ((oop *)obj)[index];
    }
    return nil;
}

static oop oopAtPut(oop obj, int index, oop value)
{
    if (obj && !isLong(obj) && !GC_atomic(obj)) {
	    int size = GC_size(obj) / sizeof(oop);
	    if ((unsigned)index < (unsigned)size) 
            return ((oop *)obj)[index]= value;
    }
    return nil;
}

static oop newExpr(oop defn, oop env)
{
    oop obj = newOops(Expr);			    GC_PROTECT(obj);
    set(obj, Expr,definition,  defn);
    set(obj, Expr,environment, env);
    set(obj, Expr,profile,     newLong(0));	GC_UNPROTECT(obj);
    return obj;
}

static oop newForm(oop fn, oop sym)	{ 
    oop obj = newOops(Form);	
    set(obj, Form,function, fn);	
    set(obj, Form,symbol, sym);	
    return obj; 
}

static oop newFixed(oop function) { 
    oop obj = newOops(Fixed);	
    set(obj, Fixed,function, function);
    return obj; 
}

static oop newSubr(wchar_t *name, imp_t imp, proto_t *sig)
{
    oop obj = newBits(Subr);
    set(obj, Subr,name,	   name);
    set(obj, Subr,imp,	   imp);
    set(obj, Subr,sig,	   sig);
    set(obj, Subr,profile, 0);
    return obj;
}

static void dump(oop);
static void dumpln(oop);
static void oprintf(char *fmt, ...);

static oop findEnvironment(oop env)
{
    while (is(Pair, env)) {
	    oop ass = getHead(env);
	    if (is(Pair, ass) && getTail(ass) == env) return env;
	    env = getTail(env);
    }
    return nil;
}

static oop findVariable2(oop env, oop name)
{
    while (env) {
	    oop ass = getHead(env);
	    if (name == car(ass)) return ass;
	    env = getTail(env);
    }
    return nil;
}

#define GLOBAL_CACHE_SIZE 2003 // 257 // 1021 // 7919

static oop _globalCache = 0;

static oop findVariable(oop env, oop name)
{
    while (env) {
	    if (env == globalNamespace) {
	        long hash = ((long)name) % GLOBAL_CACHE_SIZE;
	        oop  ent  = ((oop *)_globalCache)[hash];
	        if ((nil != ent) && (name == getHead(ent))) return ent;
	        return ((oop *)_globalCache)[hash]= findVariable2(env, name);
	    }
	    oop ass = getHead(env);
	    if (name == car(ass)) return ass;
	    env = getTail(env);
    }
    return nil;
}

static oop findNamespaceVariable(oop env, oop name)
{
    oop beg = findEnvironment(env);
    oop end = findEnvironment(cdr(env));
    while (beg != end) {
	if (beg == globalNamespace) {
	    long hash = ((long)name) % GLOBAL_CACHE_SIZE;
	    oop ent = ((oop *)_globalCache)[hash];
	    if ((nil != ent) && (name == getHead(ent))) return ent;
	    return ((oop *)_globalCache)[hash]= findVariable2(env, name);
	}
	oop ass = car(beg);
	if (name == car(ass)) return ass;
	beg = getTail(beg);
    }
    return nil;
}

static oop lookup(oop env, oop name)
{
    return cdr(findVariable(env, name));
}

static oop define(oop env, oop name, oop value)
{
    env = findEnvironment(env);					
    if (!env) fatal("failed to find an environment");
#if 0
    oop binding = findVariable(env, name);
    if (binding) {
	    setTail(binding, value);
    } else {
	    binding = cons(nil, getTail(env));
	    setTail(env, binding);
	    binding = setHead(binding, cons(name, value));
    }
#else
    oop binding = cons(nil, getTail(env));
    setTail(env, binding);
    binding = setHead(binding, cons(name, value));
#endif
    return binding;
}

static oop newBool(int b) { return b ? s_t : nil; }

static oop intern(wchar_t *string)
{
    ssize_t lo = 0, 
            hi = arrayLength(symbols) - 1, 
            c  = 0;
    oop s = nil;
    while (lo <= hi) {
	    size_t m = (lo + hi) / 2;
	    s = arrayAt(symbols, m);
	    c = wcscmp(string, get(s, Symbol,bits));
	    if (c < 0)      hi = m - 1;
	    else if (c > 0)	lo = m + 1;
	    else return s;
    }
    GC_PROTECT(s);
    s = newSymbol(string);
    arrayInsert(symbols, lo, s);
    GC_UNPROTECT(s);
    return s;
}

#include "chartab.h"

static int isPrint(int c)	{ return (0 <= c && c <= 127 && (CHAR_PRINT   & chartab[c])) || (c >= 128); }
static int isAlpha(int c)	{ return (0 <= c && c <= 127 && (CHAR_ALPHA   & chartab[c])) || (c >= 128); }
static int isDigit10(int c)	{ return (0 <= c && c <= 127 && (CHAR_DIGIT10 & chartab[c])); }
static int isDigit16(int c)	{ return (0 <= c && c <= 127 && (CHAR_DIGIT16 & chartab[c])); }
static int isLetter(int c)	{ return (0 <= c && c <= 127 && (CHAR_LETTER  & chartab[c])) || (c >= 128); }

#define DONE ((oop)-4)	/* cannot be a tagged immediate */

static void beginSource(wchar_t *path)
{
    currentPath   = newString(path);
    currentLine   = newLong(1);
    currentSource = cons(currentSource, nil);
    set(currentSource, Pair,source, cons(currentPath, currentLine));
}

static void advanceSource(void)
{
    currentLine = newLong(getLong(currentLine) + 1);
    set(currentSource, Pair,source, cons(currentPath, currentLine));
}

static void endSource(void)
{
    currentSource = get(currentSource, Pair,head);
    oop src       = get(currentSource, Pair,source);
    currentPath   = car(src);
    currentLine   = cdr(src);
}

static oop readExpr(FILE *fp);

static oop readList(FILE *fp, int delim)
{
    oop head = nil, 
        tail = head, 
        obj  = nil;
    GC_PROTECT(head);
    GC_PROTECT(obj);
    obj = readExpr(fp);
    if (obj == DONE) goto eof;
    head = tail = newPairFrom(obj, nil, currentSource);
    for (;;) {
	    obj= readExpr(fp);
	    if (obj == DONE) goto eof;
	    if (obj == s_dot) {
	        obj= readExpr(fp);
	        if (obj == DONE) fatal("missing item after .");
	        tail = set(tail, Pair,tail, obj);
	        obj = readExpr(fp);
	        if (obj != DONE) fatal("extra item after .");
	        goto eof;
	    }
	    obj  = newPairFrom(obj, nil, currentSource);
	    tail = set(tail, Pair,tail, obj);
    }
    eof:;
    int c= getwc(fp);
    if (c != delim) {
	    if (c < 0) fatal("EOF while reading list");
	    fatal("mismatched delimiter: expected '%c' found '%c'", delim, c);
    }
    GC_UNPROTECT(obj);
    GC_UNPROTECT(head);
    return head;
}

static int digitValue(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    else if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    else if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    else {
        fprintf(stderr, "illegal digit in character escape\n");
        exit(1);
    }
}

static int isHexadecimal(int c)
{
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f')) {
        return 1;
    }
    else {
        return 0;
    }
}


static int isOctal(wint_t c)
{
    return '0' <= c && c <= '7';
}

static int readChar(wint_t c, FILE *fp)
{
    if ('\\' == c) {
	    c= getwc(fp);
	    switch (c) {
	        case 'a':   return '\a';
	        case 'b':   return '\b';
	        case 'f':   return '\f';
	        case 'n':   return '\n';
	        case 'r':   return '\r';
	        case 't':   return '\t';
	        case 'v':   return '\v';
	        case 'u': {
	    	    wint_t a = getwc(fp), 
                       b = getwc(fp), 
                       c = getwc(fp), 
                       d = getwc(fp);
	    	    return (digitValue(a) << 12) + (digitValue(b) << 8) + (digitValue(c) << 4) + digitValue(d);
	        }
	        case 'x': {
	    	    int x = 0;
	    	    if (isHexadecimal(c= getwc(fp))) {
	    	        x = digitValue(c);
	    	        if (isHexadecimal(c= getwc(fp))) {
	    		        x = x * 16 + digitValue(c);
	    		        c= getwc(fp);
	    	        }
	    	    }
	    	    ungetwc(c, fp);
	    	    return x;
	        }
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
	    	    int x = digitValue(c);
	    	    if (isOctal(c = getwc(fp))) {
	    	        x = x * 8 + digitValue(c);
	    	        if (isOctal(c = getwc(fp))) {
	    		        x = x * 8 + digitValue(c);
	    		        c = getwc(fp);
	    	        }
	    	    }
	    	    ungetwc(c, fp);
	    	    return x;
	        }
	        default:
	    	    if (isAlpha(c) || isDigit10(c)) fatal("illegal character escape: \\%c", c);
	    	    return c;
	    }
    }
    return c;
}

static oop readExpr(FILE *fp)
{
    for (;;) {
	    wint_t c = getwc(fp);
	    switch (c) {
	        case WEOF: {
		        return DONE;
	        }
	        case '\n': {
		        while ('\r' == (c = getwc(fp)));
		        if (c >= 0) ungetwc(c, fp);
		        advanceSource();
		        continue;
	        }
	        case '\r': {
		        while ('\n' == (c = getwc(fp)));
		        ungetwc(c, fp);
		        advanceSource();
		        continue;
	        }
	        case '\t': 
            case ' ': {
		        continue;
	        }
	        case ';': {
		        for (;;) {
		            c = getwc(fp);
		            if (EOF == c) break;
		            if ('\n' == c || '\r' == c) {
			            ungetwc(c, fp);
			            break;
		            }
		        }
		        continue;
	        }
	        case '"': {
		        static struct buffer buf = BUFFER_INITIALISER;
		        buffer_reset(&buf);
		        for (;;) {
		            c = getwc(fp);
		            if ('"' == c) break;
		            c= readChar(c, fp);
		            if (EOF == c) fatal("EOF in string literal");
		            buffer_append(&buf, c);
		        }
		        oop obj = newString(buffer_contents(&buf));
		        //buffer_free(&buf);
		        return obj;
	        }
	        case '?': {
		        return newLong(readChar(getwc(fp), fp));
	        }
	        case '\'': {
		        oop obj = readExpr(fp);
		        if (obj == DONE) obj = s_quote;
		        else {
		            GC_PROTECT(obj);
		            obj = newPairFrom(obj, nil, currentSource);
		            obj = newPairFrom(s_quote, obj, currentSource);
		            GC_UNPROTECT(obj);
		        }
		        return obj;
	        }
	        case '`': {
		        oop obj = readExpr(fp);
		        if (obj == DONE) obj= s_quasiquote;
		        else {
		            GC_PROTECT(obj);
		            obj = newPairFrom(obj, nil, currentSource);
		            obj = newPairFrom(s_quasiquote, obj, currentSource);
		            GC_UNPROTECT(obj);
		        }
		        return obj;
	        }
	        case ',': {
		        oop sym = s_unquote;
		        c = getwc(fp);
		        if ('@' == c) sym = s_unquote_splicing;
		        else ungetwc(c, fp);
		        oop obj = readExpr(fp);
		        if (obj == DONE) obj = sym;
		        else {
		            GC_PROTECT(obj);
		            obj = newPairFrom(obj, nil, currentSource);
		            obj = newPairFrom(sym, obj, currentSource);
		            GC_UNPROTECT(obj);
		        }
		        return obj;
	        }
            case '0': case '1': case '2': case '3': case '4': 
            case '5': case '6': case '7': case '8': case '9':
	            doDigits: {
		            static struct buffer buf= BUFFER_INITIALISER;
		            buffer_reset(&buf);
		            do {
		                buffer_append(&buf, c);
		                c = getwc(fp);
		            } while (isDigit10(c));
		            if (('.' == c) || ('e' == c)) {
		                if ('.' == c) {
			                do {
			                    buffer_append(&buf, c);
			                    c = getwc(fp);
			                } while (isDigit10(c));
		                }
		                if ('e' == c) {
			                buffer_append(&buf, c);
			                c = getwc(fp);
			                if ('-' == c) {
			                    buffer_append(&buf, c);
			                    c = getwc(fp);
			                }
			                while (isDigit10(c)) {
			                    buffer_append(&buf, c);
			                    c = getwc(fp);
			                }
		                }
		                ungetwc(c, fp);
		                oop obj = newDouble(wcstod(buffer_contents(&buf), 0));
		                return obj;
		            }
		            if (('x' == c) && ((1 == buf.position) || ((2 == buf.position) && (buf.buffer[0] == '-'))))
		                do {
			                buffer_append(&buf, c);
			                c = getwc(fp);
		                } while (isDigit16(c));
		            ungetwc(c, fp);
		            oop obj = newLong(wcstoul(buffer_contents(&buf), 0, 0));
		            return obj;
	            }
	        case '(': return readList(fp, ')');	  
            case ')': ungetwc(c, fp); return DONE;
	        case '[': {
		        oop obj= readList(fp, ']');			    GC_PROTECT(obj);
		        obj = newPairFrom(s_bracket, obj, obj);	GC_UNPROTECT(obj);
		        return obj;
	        }
	        case ']': ungetwc(c, fp); return DONE;
	        case '{': {
		        oop obj = readList(fp, '}');			GC_PROTECT(obj);
		        obj = newPairFrom(s_brace, obj, obj);	GC_UNPROTECT(obj);
		        return obj;
	        }
	        case '}': ungetwc(c, fp); return DONE;
	        case '-': {
		        wint_t d= getwc(fp);
		        ungetwc(d, fp);
		        if (isDigit10(d)) goto doDigits;
		        /* fall through... */
	        }
	        default: {
		        if (isLetter(c)) {
		            static struct buffer buf= BUFFER_INITIALISER;
		            oop obj = nil;						GC_PROTECT(obj);
		            //oop in = nil;					    GC_PROTECT(in);
		            buffer_reset(&buf);
		            while (isLetter(c) || isDigit10(c)) {
			            buffer_append(&buf, c);
			            c = getwc(fp);
		            }
		            ungetwc(c, fp);
		            obj = intern(buffer_contents(&buf));
        		    GC_UNPROTECT(obj);
		        return obj;
		        }
		        fatal(isPrint(c) ? "illegal character: 0x%02x '%c'" : "illegal character: 0x%02x", c, c);
	        }
	    }
    }
}

static void doprint(FILE *stream, oop obj, int storing)
{
    if (!obj) {
	    fprintf(stream, "()");
	    return;
    }
    if (obj == globals) {
	    fprintf(stream, "<the global environment>");
	    return;
    }
#if !defined(NDEBUG)
    if (ptr2hdr(obj)->printing) {
	    fprintf(stream, "<loop>%i %i", getType(obj), ptr2hdr(obj)->printing);
	    return;
    }
    ptr2hdr(obj)->printing += 1;
#endif
    switch (getType(obj)) {
	    case Undefined:	fprintf(stream, "UNDEFINED"); break;
	    case Data: {	    
	        fprintf(stream, "<data[%i]", (int)GC_size(obj));
	        fprintf(stream, ">");
	        break;
	    }
	    case Long:	 fprintf(stream, "%"LD, getLong(obj));	 break;
	    case Double: fprintf(stream, "%lf", getDouble(obj)); break;
	    case String: {
	        if (!storing)
		        fprintf(stream, "%ls", get(obj, String,bits));
	        else {
		        wchar_t *p = get(obj, String,bits);
		        int c;
		        putc('"', stream);
		        while ((c = *p++)) {
		            if (c >= ' ')
			        switch (c) {
			            case '"':    printf("\\\"");	break;
			            case '\\':   printf("\\\\");	break;
			            default:	 putwc(c, stream);  break;
			        }
		            else fprintf(stream, "\\%03o", c);
		        }
		        putc('"', stream);
	        }
	        break;
	    }
	    case Symbol: fprintf(stream, "%ls", get(obj, Symbol,bits));	break;
	    case Pair: {
	        oop head= obj;
        #if 0
	        if (nil != get(head, Pair,source)) {
		    oop source= get(head, Pair,source);
		    oop path= car(source);
		    oop line= cdr(source);
		    fprintf(stream, "<%ls:%ld>", get(path, String,bits), getLong(line));
	        }
        #endif
	        fprintf(stream, "(");
	        for (;;) {
		        assert(is(Pair, head));
		        if (head == getVar(globals)) {
		            fprintf(stream, "<...all the globals...>");
		            head = nil;
		        }
		        else if (head == globals) {
		            fprintf(stream, "<the global association>");
		            head = nil;
		        }
		        else if (is(Pair, getHead(head))
			         && is(Symbol, getHead(getHead(head)))
			         && head == getTail(getHead(head))) {
		            fprintf(stream, "<%ls>", get(getHead(getHead(head)), Symbol,bits));
		        }
		        else
		            doprint(stream, getHead(head), storing);
		        head = cdr(head);
		        if (!is(Pair, head)) break;
		        fprintf(stream, " ");
	        }
	        if (nil != head) {
		        fprintf(stream, " . ");
		        doprint(stream, head, storing);
	        }
	        fprintf(stream, ")");
	        break;
	    }
	    case Array: {
	        int i, len= arrayLength(obj);
	        fprintf(stream, "Array<%d>(", arrayLength(obj));
	        for (i = 0;	i < len;  ++i) {
		        if (i) fprintf(stream, " ");
		        doprint(stream, arrayAt(obj, i), storing);
	        }
	        fprintf(stream, ")");
	        break;
	    }
	    case Expr: {
	        fprintf(stream, "Expr");
	        if (nil != (get(obj, Expr,name))) {
		        fprintf(stream, ".");
		        doprint(stream, get(obj, Expr,name), storing);
	        }
	        fprintf(stream, "=");
	        doprint(stream, car(get(obj, Expr,definition)), storing);
	        break;
	    }
	    case Form: {
	        fprintf(stream, "Form(");
	        doprint(stream, get(obj, Form,function), storing);
	        fprintf(stream, ", ");
	        doprint(stream, get(obj, Form,symbol), storing);
	        fprintf(stream, ")");
	        break;
	    }
	    case Fixed: {
	        if (isatty(1)) {
		        fprintf(stream, "[1m");
		        doprint(stream, get(obj, Fixed,function), storing);
		        fprintf(stream, "[m");
	        }
	        else {
		        fprintf(stream, "Fixed<");
		        doprint(stream, get(obj, Fixed,function), storing);
		        fprintf(stream, ">");
	        }
	        break;
	    }
	    case Subr: {
	        if (get(obj, Subr,name))
		        fprintf(stream, "%ls", get(obj, Subr,name));
	        else
		        fprintf(stream, "Subr<%p>", get(obj, Subr,imp));
	        break;
	    }
	    default: {
	        oop name = lookup(getVar(globals), intern(L"%type-names"));
	        if (is(Array, name)) {
		        name= arrayAt(name, getType(obj));
		        if (is(Symbol, name)) {
		            fprintf(stream, "[34m%ls[m", get(name, Symbol,bits));
		            break;
		        }
	        }
	        fprintf(stream, "<type=%i>", getType(obj));
	        break;
	    }
    }
#if !defined(NDEBUG)
    ptr2hdr(obj)->printing= 0;
#endif
}

static void fprint(FILE *stream, oop obj)	{ doprint(stream, obj, 0);  fflush(stream); }
static void print(oop obj)			        { fprint(stdout, obj); }

static void fdump(FILE *stream, oop obj)	{ doprint(stream, obj, 1);  fflush(stream); }
static void dump(oop obj)			        { fdump(stdout, obj); }

static void fdumpln(FILE *stream, oop obj)
{
    fdump(stream, obj);
    fprintf(stream, "\n");
    fflush(stream);
}

static void dumpln(oop obj)	{ fdumpln(stdout, obj); }

static oop apply(oop fun, oop args, oop ctx);

static oop concat(oop head, oop tail)
{
    if (!is(Pair, head)) return tail;
    tail= concat(getTail(head), tail);			  GC_PROTECT(tail);
    head= newPairFrom(getHead(head), tail, head); GC_UNPROTECT(tail);
    return head;
}

static void setSource(oop obj, oop src)
{
    if (!is(Pair, obj)) return;
    if (nil == get(obj, Pair,source)) set(obj, Pair,source, src);
    setSource(getHead(obj), src);
    setSource(getTail(obj), src);
}

static oop getSource(oop exp)
{
    if (is(Pair, exp)) {
	    oop src= get(exp, Pair,source);
	    if (nil != src) {
	        oop path = car(src);
	        oop line = cdr(src);
	        if (is(String, path) && is(Long, line))
		    return src;
	    }
    }
    return nil;
}

static int fprintSource(FILE *stream, oop src)
{
    if (nil != src) {
	    return fprintf(stream, "%ls:%"LD, get(car(src), String,bits), getLong(cdr(src)));
    }
    return 0;
}

static int printSource(oop exp)
{
    return fprintSource(stdout, getSource(exp));
}

static oop exlist(oop obj, oop env);

static oop findFormFunction(oop env, oop var)
{	if (!is(Symbol, var)) return nil;
    var = findVariable(env, var);		
    if (nil == var)	return nil;
    var = getVar(var);				
    if (!is(Form, var))	return nil;
    return get(var, Form,function);
}

static oop findFormSymbol(oop env, oop var)
{						assert(is(Symbol, var));
    var= findVariable(env, var);		if (nil == var)		return nil;
    var= getVar(var);				if (!is(Form, var))	return nil;
    return get(var, Form,symbol);
}

static oop expand(oop expr, oop env)
{
    if (opt_v > 1) { printf("EXPAND ");  dumpln(expr); }
    if (is(Pair, expr)) {
	    oop head= expand(getHead(expr), env); GC_PROTECT(head);
	    oop form= findFormFunction(env, head);
	    if (nil != form) {
	        head= apply(form, getTail(expr), env);
	        head= expand(head, env); GC_UNPROTECT(head);
	        if (opt_v > 1) { printf("EXPAND => ");	dumpln(head); }
	        setSource(head, get(expr, Pair,source));
	        return head;
	    }
	    oop tail= getTail(expr); GC_PROTECT(tail);
	    if (s_quote != head) tail = exlist(tail, env);
	    if (s_set == head && is(Pair, car(tail)) && is(Symbol, caar(tail))) {
	        static struct buffer buf = BUFFER_INITIALISER;
	        buffer_reset(&buf);
	        buffer_appendAll(&buf, L"set-");
	        buffer_appendAll(&buf, get(getHead(getHead(tail)), Symbol,bits));
	        head = intern(buffer_contents(&buf));
	        tail = concat(getTail(getHead(tail)), getTail(tail));
	    }
	    expr = newPairFrom(head, tail, expr);				
        GC_UNPROTECT(tail);  
        GC_UNPROTECT(head);
    } else if (is(Symbol, expr)) {
	    oop form = findFormSymbol(env, expr);
	    if (form) {
	        oop args = cons(expr, nil);		GC_PROTECT(args);
	        args = apply(form, args, nil);
	        args = expand(args, env);		GC_UNPROTECT(args);
	        setSource(args, expr);
	        if (opt_v > 1) { printf("EXPAND => ");	dumpln(args); }
	        return args;
	    }
    }
    if (opt_v > 1) { printf("EXPAND => ");  dumpln(expr); }
    return expr;
}

static oop exlist(oop list, oop env)
{
    if (!is(Pair, list)) return expand(list, env);
    oop head = expand(getHead(list), env);	GC_PROTECT(head);
    oop tail = exlist(getTail(list), env);	GC_PROTECT(tail);
    head = newPairFrom(head, tail, list);	GC_UNPROTECT(tail);  GC_UNPROTECT(head);
    return head;
}

static int encodeIndent= 16;

static oop enlist(oop obj, oop env);

static oop encodeFrom(oop from, oop obj, oop env)
{
    switch (getType(obj)) {
	    case Symbol: {
	        if (nil == findVariable(env, obj)) {
		    int flags = get(obj, Symbol,flags);
		        if (0 == (1 & flags)) {
		            set(obj, Symbol,flags, 1 | flags);
		            oop src = getSource(from);
		            if (nil != src) {
			        int i = fprintSource(stderr, getSource(from));
			        if (i > encodeIndent) encodeIndent = i;
			        while (i++ <= encodeIndent) putc(' ', stderr);
		            }
		            oprintf("warning: possibly undefined: %P\n", obj);
		        }
	        }
	        break;
	    }
	    case Pair: {
	        oop head= getHead(obj);
	        if (head == s_quote) {}
	        else if (head == s_define) {			
                GC_PROTECT(env);
		        env = cons(nil, env);
		        setHead(env, cons(cadr(obj), nil));
		        enlist(cddr(obj), env);				
                GC_UNPROTECT(env);
	        } else if (head == s_lambda) {			
                GC_PROTECT(env);
		        oop bindings = cadr(obj);
		        while (isPair(bindings)) {
		            oop id = bindings;
		            while (isPair(id)) id = car(id);
		            env = cons(nil, env);
		            setHead(env, cons(id, nil));
		            bindings = getTail(bindings);
		        }
		        if (is(Symbol, bindings)) {
		            env = cons(nil, env);
		            setHead(env, cons(bindings, nil));
		        }
		        enlist(cddr(obj), env);
                GC_UNPROTECT(env);
	        } else if (head == s_let) {				
                GC_PROTECT(env);
		        oop bindings = cadr(obj);
		        while (isPair(bindings)) {
		            enlist(cdar(bindings), env);
		            bindings = getTail(bindings);
		        }
		        bindings = cadr(obj);
		        while (isPair(bindings)) {
		            oop id = bindings;
		            while (isPair(id)) id= car(id);
		            env = cons(nil, env);
		            setHead(env, cons(id, nil));
		            bindings = getTail(bindings);
		        }
		        enlist(cddr(obj), env);				
                GC_UNPROTECT(env);
	        } else {
		        enlist(obj, env);
		        if (is(Symbol, head)) {
		            oop val = lookup(getVar(globals), head);
		            if (is(Expr, val)) {
			            oop formal = car(get(val, Expr,definition));
			            oop actual = cdr(obj);
			            while (isPair(formal) && isPair(actual)) {
			                if (s_etc == car(formal)) {
				                formal= actual= nil;
			                } else {
				                formal = cdr(formal);
				                actual = cdr(actual);
			                }
			            }
			            if (is(Symbol, formal)) formal = actual = nil;
			            if (nil != formal || nil != actual) {
			                oop src = getSource(obj);
			                if (nil != src) {
			    	            int i = fprintSource(stderr, getSource(from));
			    	            if (i > encodeIndent) encodeIndent = i;
			    	            while (i++ <= encodeIndent) putc(' ', stderr);
			                }
			                oprintf("warning: argument mismatch: %P -> %P\n", obj, car(get(val, Expr,definition)));
			            }
			    // CHECK ARG LIST HERE
		            }
		        }
	        }
	        break;
	    }
    }
    return obj;
}

static oop encode(oop obj, oop env)
{
    return encodeFrom(nil, obj, env);
}

static oop enlist(oop obj, oop env)
{
    while (isPair(obj)) {
	    encodeFrom(obj, getHead(obj), env);
	    obj = getTail(obj);
    }
    return obj;
}

#if 0
 static void define_bindings(oop bindings, oop innerEnv)
 {	
     GC_PROTECT(bindings);
     while (is(Pair, bindings))
     {
	    oop var = getHead(bindings);			GC_PROTECT(var);
	    if (!is(Symbol, var)) var = car(var);
	    var= define(innerEnv, var, nil);		GC_UNPROTECT(var);
	    bindings = getTail(bindings);
     }										
     GC_UNPROTECT(bindings);
 }

 static oop encode_bindings(oop expr, oop bindings, oop outerEnv, oop innerEnv)
 {
     if (is(Pair, bindings))
     {										
          GC_PROTECT(bindings);
	      oop binding = getHead(bindings);						
          GC_PROTECT(binding);
	      if (is(Symbol, binding)) 
              binding = newPairFrom(binding, nil, expr);
	      oop var = car(binding);							
          GC_PROTECT(var);
	      oop val = cdr(binding);							
          GC_PROTECT(val);
	      var = findLocalVariable(innerEnv, var);					
          assert(nil != var);
	      val = enlist(val, outerEnv);
	      binding  = newPairFrom(var, val, expr);					
          GC_UNPROTECT(val);  
          GC_UNPROTECT(var);
	      oop rest = encode_bindings(expr, getTail(bindings), outerEnv, innerEnv); 
          GC_PROTECT(rest);
	      bindings = newPairFrom(binding, rest, expr);				
          GC_UNPROTECT(rest);  
          GC_UNPROTECT(binding);  
          GC_UNPROTECT(bindings);
     }
     return bindings;
 }

 static oop encode_let(oop expr, oop tail, oop env)
 {
     oop args = car(tail);							
     GC_PROTECT(tail);  
     GC_PROTECT(env);
     oop env2 = newEnv(env, 0, getLong(get(env, Env,offset)));			
     GC_PROTECT(env2);
     define_bindings(args, env2);
     set(env, Env,offset, newLong(getLong(get(env2, Env,offset))));
     oop bindings = encode_bindings(expr, args, env, env2);			
     GC_PROTECT(bindings);
     oop body = cdr(tail);							
     GC_PROTECT(body);
     body = enlist(body, env2);
     tail = newPairFrom(bindings, body, expr);					
     GC_UNPROTECT(body);  
     GC_UNPROTECT(bindings);
     tail = newPairFrom(env2, tail, expr);					
     GC_UNPROTECT(env2);  
     GC_UNPROTECT(env);	 
     GC_UNPROTECT(tail);
     return tail;
 }

 static oop encode(oop expr, oop env)
 {
     if (opt_O < 2) arrayAtPut(traceStack, traceDepth++, expr);
     if (opt_v > 1) { 
         printf("ENCODE ");  
         dumpln(expr); 
     }
     if (is(Pair, expr)) {
         oop head = encode(getHead(expr), env);		GC_PROTECT(head);
         oop tail = getTail(expr);					GC_PROTECT(tail);
         if (f_let == head) {
	         tail= encode_let(expr, tail, env);
         } else if (f_lambda == head) {
	         oop args = car(tail);
	         env = newEnv(env, 1, 0);				GC_PROTECT(env);
	         while (is(Pair, args)) {
	             if (!is(Symbol, getHead(args))) {
	                 fprintf(stderr, "\nerror: non-symbol parameter name: ");
	                 fdumpln(stderr, getHead(args));
	                 fatal(0);
	             }
	             define(env, getHead(args), nil);
	             args= getTail(args);
	         }
	         if (nil != args) {
	             if (!is(Symbol, args)) {
	                 fprintf(stderr, "\nerror: non-symbol parameter name: ");
	                 fdumpln(stderr, args);
	                 fatal(0);
	             }
	             define(env, args, nil);
	         }
	         tail = enlist(tail, env);
	         tail = newPairFrom(env, tail, expr);	GC_UNPROTECT(env);
        } else if (f_define == head) {
	        oop var= define(get(globals, Variable,value), car(tail), nil);
	        tail = enlist(cdr(tail), env);
	        tail = newPairFrom(var, tail, expr);
        } else if (f_set == head) {
	        oop var= findVariable(env, car(tail));
	        if (nil == var) fatal("set: undefined variable: %ls", get(car(tail), Symbol,bits));
	        tail = enlist(cdr(tail), env);
	        tail = newPairFrom(var, tail, expr);
        } else if (f_quote != head)
	        tail = enlist(tail, env);
        expr= newPairFrom(head, tail, expr);		GC_UNPROTECT(tail);  
        GC_UNPROTECT(head);
    } else if (is(Symbol, expr)) {
        oop val = findVariable(env, expr);
        if (nil == val) fatal("undefined variable: %ls", get(expr, Symbol,bits));
        expr = val;
        if (isGlobal(expr)) {
	        val= get(expr, Variable,value);
	        if (is(Form, val) || is(Fixed, val))
	            expr= val;
        } else {
	        oop venv = get(val, Variable,env);
	        if (getLong(get(venv, Env,level)) != getLong(get(env, Env,level)))
	        set(venv, Env,stable, s_t);
        }
   } else {
        oop fn = arrayAt(get(encoders, Variable,value), getType(expr));
        if (nil != fn) {
	        oop args = newPair(env, nil);		GC_PROTECT(args);
	        args = newPair(expr, args);
	        expr = apply(fn, args, nil);		GC_UNPROTECT(args);
        }
   }
   if (opt_v > 1) { printf("ENCODE => ");  dumpln(expr); }
   --traceDepth;
   return expr;
}  

static oop enlist(oop list, oop env)
{
    if (!is(Pair, list)) return encode(list, env);
    oop head = encode(getHead(list), env);			GC_PROTECT(head);
    oop tail = enlist(getTail(list), env);			GC_PROTECT(tail);
    head = newPairFrom(head, tail, list);			GC_UNPROTECT(tail);  
    GC_UNPROTECT(head);
    return head;
}
#endif

static void vfoprintf(FILE *out, char *fmt, va_list ap)
{
    int c;
    while ((c = *fmt++)) {
	    if ('%' == c) {
	        char fbuf[32];
	        int  index = 0;
	        fbuf[index++] = '%';
	        while (index < sizeof(fbuf) - 1) {
		        c = *fmt++;
		        fbuf[index++] = c;
		        if (strchr("PdiouxXDOUeEfFgGaACcSspn%", c)) break;
	        }
	        fbuf[index] = 0;
	        switch (c) {	// cannot call vfprintf(out, fbuf, ap) because ap can be passed by copy
		        case 'e': case 'E': case 'f': case 'F':
		        case 'g': case 'G': case 'a': case 'A':	{ double arg= va_arg(ap, double);	fprintf(out, fbuf, arg); break; }
		        case 'd': case 'i': case 'o': case 'u':
		        case 'x': case 'X': case 'c':		    { int    arg= va_arg(ap, int);		fprintf(out, fbuf, arg); break; }
		        case 'D': case 'O': case 'U':		    { long   arg= va_arg(ap, long int);	fprintf(out, fbuf, arg); break; }
		        case 'C':				                { wint_t arg= va_arg(ap, wint_t);	fprintf(out, fbuf, arg); break; }
		        case 's': case 'S': case 'p':		    { void  *arg= va_arg(ap, void *);	fprintf(out, fbuf, arg); break; }
		        case 'P':				                { oop    arg= va_arg(ap, oop);		fdump  (out,       arg); break; }
		        case '%':				                {					                putc('%', out);		     break; }
		        default:
		            fprintf(stderr, "\nimplementation error: cannot convert format '%c'\n", c);
		            exit(1);
		            break;
	        }
	    } else fputc(c, out);
    }
}

static void oprintf(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfoprintf(stderr, fmt, ap);
    va_end(ap);
}

static void fatal(char *reason, ...)
{
    if (reason) {
	    va_list ap;
	    va_start(ap, reason);
	    fprintf(stderr, "\nerror: ");
	    vfoprintf(stderr, reason, ap);
	    fprintf(stderr, "\n");
	    va_end(ap);
    }

    oop tracer = getVar(backtrace);

    if (nil != tracer) {
	    oop args = newLong(traceDepth);		GC_PROTECT(args);
	    args = cons(args, nil);
	    args = cons(traceStack, args);
	    apply(tracer, args, nil);			GC_UNPROTECT(args);
    } else {
	    if (traceDepth) {
	        int i = traceDepth;
	        int j = 12;
	        while (i-- > 0) {		    
		        oop exp = arrayAt(traceStack, i);
		        fprintf(stderr, "[32m[?7l");
		        int l = fprintSource(stderr, getSource(exp));
		        if (l >= j) j = l;
		        if (!l) while (l < 3) l++, fputc('.', stderr);
		        while (l++ < j) fputc(' ', stderr);
		        fprintf(stderr, "[0m ");
		        fdumpln(stderr, arrayAt(traceStack, i));
		        fprintf(stderr, "[?7h");
	        }
	    }
	    else {
	        fprintf(stderr, "no backtrace\n");
	    }
    }
    exit(1);
}

static oop evlist(oop obj, oop env);

static oop eval(oop obj, oop env)
{
    if (opt_v > 2) { printf("EVAL ");  dumpln(obj); }
    switch (getType(obj)) {
	    case Undefined:
	    case Long:
	    case Double:
	    case String:
	    case Form:
	    case Subr:
	    case Fixed: {
	        return obj;
	    }
	    case Pair: {
	        if (opt_O < 2) arrayAtPut(traceStack, traceDepth++, obj);
	        oop head= eval(getHead(obj), env);						
            GC_PROTECT(head);
	        if (is(Fixed, head))
		        head= apply(get(head, Fixed,function), getTail(obj), env);
	        else {
		        oop args = evlist(getTail(obj), env);					
                GC_PROTECT(args);
		        if (opt_g) arrayAtPut(traceStack, traceDepth++, cons(head, args));
		        head = apply(head, args, env);						
                GC_UNPROTECT(args);
		        if (opt_g) --traceDepth;
	        }										
            GC_UNPROTECT(head);
	        --traceDepth;
	        return head;
	    }
	    case Symbol: {
	        oop ass= findVariable(env, obj);
	        if (nil == ass) fatal("eval: undefined variable: %P", obj);
	        return getTail(ass);
	    }
	    default: {
	        fatal("cannot eval (%i): %P", getType(obj), obj);
	    }
    }
    return nil;
}

static oop evlist(oop obj, oop env)
{
    if (!is(Pair, obj)) return obj;
    oop head = eval(getHead(obj), env);			GC_PROTECT(head);
    oop tail = evlist(getTail(obj), env);		GC_PROTECT(tail);
    head = cons(head, tail);				    GC_UNPROTECT(tail);  GC_UNPROTECT(head);
    return head;
}

static oop ffcall(oop subr, oop arguments);

static oop apply(oop fun, oop arguments, oop env)
{
    if (opt_v > 2) oprintf("APPLY %P TO %P\n", fun, arguments);
    int funType = getType(fun);
    switch (funType) {
	    case Expr: {
	        if (opt_p) arrayAtPut(traceStack, traceDepth++, fun);
	        oop defn	= get(fun, Expr,definition);			GC_PROTECT(defn);
	        oop formals	= car(defn);
	        oop actuals	= arguments;
	        oop caller	= env;						            GC_PROTECT(caller);
	        oop callee	= get(fun, Expr,environment);			GC_PROTECT(callee);
	        oop tmp	= nil;						                GC_PROTECT(tmp);
	        while (is(Pair, formals)) {
		        if (!is(Pair, actuals))	fatal("too few arguments applying %P to %P", fun, arguments);
		        tmp     = cons(getHead(formals), getHead(actuals));
		        callee  = cons(tmp, callee);
		        formals = getTail(formals);
		        actuals = getTail(actuals);
	        }
	        if (is(Symbol, formals)) {
		        tmp     = cons(formals, actuals);
		        callee  = cons(tmp, callee);
		        actuals = nil;
	        }
	        if (nil != actuals) fatal("too many arguments applying %P to %P", fun, arguments);
    #if LOCALS_ARE_NAMESPACE
	        tmp         = cons(s_locals, nil);
	        callee      = cons(tmp, callee);
	        setTail(tmp, callee);
    #endif
	        oop ans     = nil;
	        oop body    = cdr(defn);
	        if (opt_g) {
		        arrayAtPut(traceStack, traceDepth++, body);
		        if (traceDepth > 2000) fatal("infinite recursion suspected");
	        }
	        while (is(Pair, body)) {
		        ans  = eval(getHead(body), callee);
		        body = getTail(body);
	        }
	        if (opt_g || opt_p) --traceDepth;
	        GC_UNPROTECT(tmp);
	        GC_UNPROTECT(callee);
	        GC_UNPROTECT(caller);
	        GC_UNPROTECT(defn);
	        return ans;
	    }
	    case Fixed: {
	        return apply(get(fun, Fixed,function), arguments, env);
	    }
	    case Subr: {
	        if (opt_p) arrayAtPut(traceStack, traceDepth++, fun);
	        oop ans = get(fun, Subr,sig) ? ffcall(fun, arguments) : get(fun, Subr,imp)(arguments, env);
	        if (opt_p) --traceDepth;
	        return ans;
	    }
	    default: {
	        oop args = arguments;
	        oop ap   = arrayAt(getVar(applicators), funType);
	        if (nil != ap) {
                GC_PROTECT(args);
		        if (opt_g) arrayAtPut(traceStack, traceDepth++, fun);
		        args = cons(fun, args);
		        args = apply(ap, args, env);				
                GC_UNPROTECT(args);
		        if (opt_g) --traceDepth;
		        return args;
	        }
	        fatal("error: cannot apply: %P", fun);
	    }
    }
    return nil;
}

static ffi_type ffi_type_long;

#define ffcast(NAME, OTYPE)												                                                    \
    static void ff##NAME(oop arg, void **argp, arg_t *buf)									                                \
    {															                                                            \
	switch (getType(arg)) {													                                                \
	    case OTYPE:	buf->arg_##NAME= get##OTYPE(arg);  *argp= &buf->arg_##NAME;	break;	                                    \
	    default:	fprintf(stderr, "\nnon-"#OTYPE" argument in foreign call: ");  fdumpln(stderr, arg);  fatal(0);	 break;	\
	    }															                                                        \
    }

ffcast(int,	    Long)
ffcast(int32,	Long)
ffcast(int64,	Long)
ffcast(long,	Long)
ffcast(float,	Double)
ffcast(double,	Double)

#undef ffcast

static void ffpointer(oop arg, void **argp, arg_t *buf)
{
    void *ptr = 0;
    switch (getType(arg)) {
	    case Undefined:	ptr = 0;					        break;
	    case Data:	    ptr = (void *)arg;			        break;
	    case Long:	    ptr = (void *)(long)getLong(arg);	break;
	    case Double:	ptr = (void *)arg;			        break;
	    case String:	ptr = get(arg, String,bits);		break;
	    case Symbol:	ptr = get(arg, Symbol,bits);		break;
	    case Expr:	    ptr = (void *)arg;			        break;
	    case Subr:	    ptr = get(arg, Subr,imp);		    break;
	    default:
	        if (GC_atomic(arg))
	    	    ptr = (void *)arg;
	        else {
	    	    fprintf(stderr, "\ninvalid pointer argument: ");
	    	    fdumpln(stderr, arg);
	    	    fatal(0);
	        }
	        break;
    }
    buf->arg_pointer = ptr;
    *argp = &buf->arg_pointer;
}

static void ffstring(oop arg, void **argp, arg_t *buf)
{
    if (!is(String, arg)) {
	    fprintf(stderr, "\nnon-String argument in foreign call: ");
	    fdumpln(stderr, arg);
	    fatal(0);
    }
    buf->arg_string = wcs2mbs(get(arg, String,bits));
    *argp= &buf->arg_string;
}

static ffi_type *ffdefault(oop arg, void **argp, arg_t *buf)
{
    switch (getType(arg))
    {
	    case Undefined:	buf->arg_pointer= 0;					           *argp = &buf->arg_pointer;	return &ffi_type_pointer;
	    case Long:	    buf->arg_long    = getLong(arg);				   *argp = &buf->arg_long;		return &ffi_type_long;
	    case Double:	buf->arg_double  = getDouble(arg);			       *argp = &buf->arg_double;	return &ffi_type_double;
	    case String:	buf->arg_string  = wcs2mbs(get(arg, String,bits)); *argp = &buf->arg_string;	return &ffi_type_pointer;
	    case Subr:	    buf->arg_pointer = get(arg, Subr,imp);			   *argp = &buf->arg_pointer;	return &ffi_type_pointer;
    }
    fprintf(stderr, "\ncannot pass object through '...': ");
    fdumpln(stderr, arg);
    fatal(0);
    return 0;
}

static oop ffcall(oop subr, oop arguments)
{
    proto_t	 *sig      = get(subr, Subr,sig);
    imp_t	 imp       = get(subr, Subr,imp);
    oop	     argp      = arguments;
    int	     arg_count = 0;
    void	 *args[32];
    arg_t	 bufs[32];
    ffi_cif	 cif;
    ffi_type ret_type = ffi_type_pointer;
    ffi_arg	 result;
    ffi_type *arg_types[32];
    while ((arg_count < sig->arg_count) && (nil != argp)) {
	    sig->arg_casts[arg_count](car(argp), &args[arg_count], &bufs[arg_count]);
	    arg_types[arg_count]= sig->arg_types[arg_count];
	    ++arg_count;
	    argp = getTail(argp);
    }
    if (arg_count != sig->arg_count) 
        fatal("too few arguments (%i < %i) in call to %S", arg_count, sig->arg_count, get(subr, Subr,name));
    if (sig->arg_rest) {
	    while ((nil != argp) && (arg_count < 32)) {
	        arg_types[arg_count] = ffdefault(car(argp), &args[arg_count], &bufs[arg_count]);
	        ++arg_count;
	        argp = getTail(argp);
	    }
    }
    if (nil != argp) 
        fatal("too many arguments in call to %S", get(subr, Subr,name));
    if (FFI_OK != ffi_prep_cif(&cif, FFI_DEFAULT_ABI, arg_count, &ret_type, arg_types)) 
        fatal("FFI call setup failed");
    ffi_call(&cif, FFI_FN(imp), &result, args);
    return newLong((long)result);
}

static int length(oop list)
{
    if (!is(Pair, list)) return 0;
    return 1 + length(getTail(list));
}

static void arity(oop args, char *name)
{
    fatal("wrong number of arguments (%i) in: %s\n", length(args), name);
}

static void arity1(oop args, char *name)
{
    if (!is(Pair, args) || is(Pair, getTail(args))) arity(args, name);
}

static void arity2(oop args, char *name)
{
    if (!is(Pair, args) || !is(Pair, getTail(args)) || is(Pair, getTail(getTail(args)))) arity(args, name);
}

static void arity3(oop args, char *name)
{
    if (!is(Pair, args) || !is(Pair, getTail(args)) || !is(Pair, getTail(getTail(args))) || is(Pair, getTail(getTail(getTail(args))))) arity(args, name);
}

#define subr(NAME)	oop subr_##NAME(oop args, oop env)

static subr(if)
{
    if (nil != eval(car(args), env))
	    return eval(cadr(args), env);
    oop ans = nil;
    args = cddr(args);
    while (is(Pair, args)) {
	    ans = eval(getHead(args), env);
	    args = cdr(args);
    }
    return ans;
}

static subr(and)
{
    oop ans= s_t;
    for (;  is(Pair, args);  args = getTail(args))
	if (nil == (ans= eval(getHead(args), env)))
	    break;
    return ans;
}

static subr(or)
{
    oop ans = nil;
    for (;  is(Pair, args);  args= getTail(args))
	if (nil != (ans= eval(getHead(args), env)))
	    break;
    return ans;
}

static subr(set)
{
    oop sym = car(args);				if (!is(Symbol, sym)) fatal("non-symbol in set: %P", sym);
    oop var = findVariable(env, sym);	if (nil == var)		  fatal("set: undefined variable: %P", sym);
    oop val = eval(cadr(args), env);	if (is(Expr, val) && (nil == get(val, Expr,name))) set(val, Expr,name, sym);
    setTail(var, val);
    return val;
}

static subr(let)
{
    oop bound    = cons(nil, nil);			GC_PROTECT(bound);
    oop ptr      = bound;
    oop bindings = car(args);
    oop tmp      = nil;					    GC_PROTECT(tmp);
    while (isPair(bindings)) {
	    oop binding = getHead(bindings);
	    oop name    = 0;
	    if (isPair(binding)) {
	        name = car(binding);
	        tmp  = eval(cadr(binding), env);
	    } else {
	        if (!is(Symbol, binding)) fatal("let: non-symbol identifier: %P", binding);
	        name = binding;
	        tmp  =  nil;
	    }
	    ptr = setTail(ptr, cons(nil, nil));
	    setHead(ptr, cons(name, tmp));
	    bindings = getTail(bindings);
    }
    setTail(ptr, env);
#if LOCALS_ARE_NAMESPACE
    setHead(bound, cons(s_locals, bound));
#else
    bound = getTail(bound);
#endif
    oop body = cdr(args);
    tmp = nil;						        GC_UNPROTECT(tmp);
    while (is(Pair, body)) {
	    oop exp = getHead(body);
	    tmp = eval(exp, bound);
	    body = getTail(body);
    }							            GC_UNPROTECT(bound);
    return tmp;
}

static subr(while)
{
    oop tst = car(args);
    while (nil != eval(tst, env)) {
	    oop body = cdr(args);
	    while (is(Pair, body)) {
	        eval(getHead(body), env);
	        body = getTail(body);
	    }
    }
    return nil;
}

static subr(quote)
{
    return car(args);
}

static subr(lambda)
{
    return newExpr(args, env);
}

static subr(define)
{
    oop sym = car(args);
    oop val = eval(cadr(args), env);			GC_PROTECT(val);
    oop var = findNamespaceVariable(env, sym);
    if (nil == var)
	    var = define(env, sym, val);
    else
	    setTail(var, val);
    if (is(Form, val)) val = get(val, Form,function);
    if (is(Expr, val) && (nil == get(val, Expr,name)))
	    set(val, Expr,name, sym);			    GC_UNPROTECT(val);
    return val;
}

static subr(definedP)
{
    oop symbol = car(args);
    oop theenv = cadr(args);
    if (nil == theenv) theenv = getVar(globals);
    return findVariable(theenv, symbol);
}

#define _do_ibinary() \
    _do(bitand, &) _do(bitor, |)  _do(bitxor, ^) _do(shl, <<) _do(shr, >>)

#define _do(NAME, OP)								                \
    static subr(NAME)								                \
    {										                        \
	    arity2(args, #OP);							                \
	    oop lhs = getHead(args);							        \
	    oop rhs = getHead(getTail(args));					        \
	    if (isLong(lhs) && isLong(rhs))						        \
	        return newLong(getLong(lhs) OP getLong(rhs));			\
	    fprintf(stderr, "%s: non-integer argument: ", #OP);			\
	    if (!isLong(lhs)) fdumpln(stderr, lhs);				        \
	    else fdumpln(stderr, rhs);					                \
	    fatal(0);								                    \
	    return nil;								                    \
    }

_do_ibinary()

#undef _do

#define _do_binary() _do(add, +) _do(mul, *)  _do(div, /)

#define _do(NAME, OP)										                                \
    static subr(NAME)										                                \
    {												                                        \
	    arity2(args, #OP);									                                \
	    oop lhs = getHead(args);									                        \
	    oop rhs = getHead(getTail(args));							                        \
	    if (isLong(lhs)) {									                                \
	        if (isLong(rhs))   return newLong(getLong(lhs) OP getLong(rhs));			    \
	        if (isDouble(rhs)) return newDouble((double)getLong(lhs) OP getDouble(rhs));	\
	    } else if (isDouble(lhs)) {								                            \
	        if (isDouble(rhs)) return newDouble(getDouble(lhs) OP getDouble(rhs));		    \
	        if (isLong(rhs))   return newDouble(getDouble(lhs) OP (double)getLong(rhs));	\
	    }											                                        \
	    fatal("%s: non-numeric argument: %P", #OP, (isNumeric(lhs) ? rhs : lhs));		    \
	    return nil;										                                    \
    }

_do_binary()

#undef _do

static subr(sub)
{
    if (!is(Pair, args)) arity(args, "-");
    oop lhs = getHead(args);	args= getTail(args);
    if (!is(Pair, args)) {
	    if (isLong  (lhs)) return newLong  (- getLong  (lhs));
	    if (isDouble(lhs)) return newDouble(- getDouble(lhs));
	    fprintf(stderr, "-: non-numeric argument: ");
	    fdumpln(stderr, lhs);
	    fatal(0);
    }
    oop rhs = getHead(args); args = getTail(args);
    if (is(Pair, args)) arity(args, "-");
    if (isLong(lhs)) {
	    if (isLong(rhs))   return newLong(getLong(lhs) - getLong(rhs));
	    if (isDouble(rhs)) return newDouble((double)getLong(lhs) - getDouble(rhs));
    }
    if (isDouble(lhs)) {
	    if (isDouble(rhs)) return newDouble(getDouble(lhs) - getDouble(rhs));
	    if (isLong(rhs))   return newDouble(getDouble(lhs) - (double)getLong(rhs));
	    lhs = rhs; // for error msg
    }
    fprintf(stderr, "-: non-numeric argument: ");
    fdumpln(stderr, lhs);
    fatal(0);
    return nil;
}

static subr(mod)
{
    if (!is(Pair, args)) arity(args, "%");
    oop lhs = getHead(args);	
    args = getTail(args);
    if (!is(Pair, args)) arity(args, "%");
    oop rhs = getHead(args);	
    args = getTail(args);
    if (is(Pair, args)) arity(args, "%");
    if (isLong(lhs)) {
	    if (isLong(rhs))	return newLong(getLong(lhs) % getLong(rhs));
	    if (isDouble(rhs))	return newDouble(fmod((double)getLong(lhs), getDouble(rhs)));
    } else if (isDouble(lhs)) {
	    if (isDouble(rhs))	return newDouble(fmod(getDouble(lhs), getDouble(rhs)));
	    if (isLong(rhs))	return newDouble(fmod(getDouble(lhs), (double)getLong(rhs)));
    }
    fprintf(stderr, "%%: non-numeric argument: ");
    if (!isNumeric(lhs)) fdumpln(stderr, lhs);
    else fdumpln(stderr, rhs);
    fatal(0);
    return nil;
}

#define _do_relation() _do(lt, <)  _do(le, <=) _do(ge, >=)  _do(gt,	>)

#define _do(NAME, OP)											                            \
    static subr(NAME)											                            \
    {													                                    \
	    arity2(args, #OP);										                            \
	    oop lhs = getHead(args);										                    \
	    oop rhs = getHead(getTail(args));								                    \
	    if (isLong(lhs)) {										                            \
	        if (isLong(rhs))	return newBool(getLong(lhs) OP getLong(rhs));				\
	        if (isDouble(rhs))	return newBool((double)getLong(lhs) OP getDouble(rhs));		\
	        lhs = rhs;											                            \
	    } else if (isDouble(lhs)) {									                        \
	        if (isDouble(rhs))	return newBool(getDouble(lhs) OP getDouble(rhs));			\
	        if (isLong(rhs))	return newBool(getDouble(lhs) OP (double)getLong(rhs));		\
	        lhs = rhs;											                            \
	    }												                                    \
	    fatal("%s: non-numeric argument: %P", #OP, lhs);						            \
	    return nil;											                                \
    }

_do_relation()

#undef _do

static int equal(oop lhs, oop rhs)
{
    int ans= 0;
    switch (getType(lhs)) {
	    case Long:
	        switch (getType(rhs)) {
		        case Long:	 ans = (	    getLong (lhs) == getLong  (rhs)); break;
		        case Double: ans = ((double)getLong (lhs) == getDouble(rhs)); break;
	        }
	        break;
	    case Double:
	        switch (getType(rhs)) {
		        case Long:	 ans = (getDouble(lhs) == (double)getLong (rhs)); break;
		        case Double: ans = (getDouble(lhs) == getDouble(rhs));        break;
	        }
	        break;
	    case String: ans = (is(String, rhs) && !wcscmp(get(lhs, String,bits), get(rhs, String,bits))); break;
	    default:	 ans= (lhs == rhs);	break;
    }
    return ans;
}

static subr(eq)
{
    arity2(args, "=");
    oop lhs = getHead(args);
    oop rhs = getHead(getTail(args));
    return newBool(equal(lhs, rhs));
}

static subr(ne)
{
    arity2(args, "!=");
    oop lhs = getHead(args);
    oop rhs = getHead(getTail(args));
    return newBool(!equal(lhs, rhs));
}

static subr(exit)
{
    oop n= car(args);
    exit(isLong(n) ? getLong(n) : 0);
}

static subr(abort)
{
    fatal("aborting");
    return nil;
}

static subr(open)
{
    oop arg = car(args);
    if (!is(String, arg)) { fprintf(stderr, "open: non-string argument: ");  fdumpln(stderr, arg);  fatal(0); }
    char *name = strdup(wcs2mbs(get(arg, String,bits)));
    char *mode = "r";
    long  wide = 1;
    if (is(String, cadr(args))) mode = wcs2mbs(get(cadr(args), String,bits));
    if (is(Long, caddr(args)))  wide = getLong(caddr(args));
    FILE *stream = (FILE *)fopen(name, mode);
    free(name);
    if (stream) fwide(stream, wide);
    return stream ? newLong((long)stream) : nil;
}

static subr(close)
{
    oop arg = car(args);
    if (!isLong(arg)) { fprintf(stderr, "close: non-integer argument: ");  fdumpln(stderr, arg);  fatal(0); }
    fclose((FILE *)(long)getLong(arg));
    return arg;
}

static subr(getc)
{
    oop arg = car(args);
    if (nil == arg) arg = getVar(input);
    if (!isLong(arg)) { fprintf(stderr, "getc: non-integer argument: ");  fdumpln(stderr, arg);  fatal(0); }
    FILE *stream = (FILE *)(long)getLong(arg);
    int c = getwc(stream);
    return (WEOF == c) ? nil : newLong(c);
}

static subr(putc)
{
    oop chr = car(args);
    oop arg = cadr(args);
    if (nil == arg) arg = getVar(output);
    if (!isLong(chr)) { fprintf(stderr, "putc: non-integer character: "); fdumpln(stderr, chr); fatal(0); }
    if (!isLong(arg)) { fprintf(stderr, "putc: non-integer argument: ");  fdumpln(stderr, arg); fatal(0); }
    FILE *stream = (FILE *)(long)getLong(arg);
    int c = putwc(getLong(chr), stream);
    return (WEOF == c) ? nil : chr;
}

static subr(read)
{
    FILE *stream = stdin;
    oop   head   = nil;
    if (nil == args) {
	    beginSource(L"<stdin>");
	    oop obj = readExpr(stdin);
	    endSource();
	    if (obj == DONE) obj = nil;
	    return obj;
    }
    oop arg = car(args);
    if (is(String, arg)) {
	    wchar_t *path = get(arg, String,bits);
	    stream = fopen(wcs2mbs(path), "r");
	    if (!stream) return nil;
	    fwide(stream, 1);
	    beginSource(path);
	    head = newPairFrom(nil, nil, currentSource); GC_PROTECT(head);
	    oop tail = head;
	    oop obj  = nil;					             GC_PROTECT(obj);
	    for (;;) {
	        obj = readExpr(stream);
	        if (obj == DONE) break;
	        tail = setTail(tail, newPairFrom(obj, nil, currentSource));
	        if (stdin == stream) break;
	    }
	    head = getTail(head);				         GC_UNPROTECT(obj);
	    fclose(stream);					             GC_UNPROTECT(head);
	    endSource();
    } else if (isLong(arg)) {
	    stream = (FILE *)(long)getLong(arg);
	    if (stream) head = readExpr(stream);
	    if (head == DONE) head = nil;
    } else {
	    fprintf(stderr, "read: non-String/Long argument: ");
	    fdumpln(stderr, arg);
	    fatal(0);
    }
    return head;
}

static subr(expand)
{
    oop x = car(args);	args= cdr(args);	GC_PROTECT(x);
    oop e = car(args);
    if (nil == e) e= env;
    x = expand(x, e);				        GC_UNPROTECT(x);
    return x;
}

static subr(eval)
{
    oop x = car(args);	args= cdr(args);	GC_PROTECT(x);
    oop e= car(args);
    if (nil == e) e = getVar(globals);		GC_PROTECT(e);
    x = expand(x, e);
    if (opt_g) encode(x, e);
    x = eval(x, e);						    GC_UNPROTECT(e);  GC_UNPROTECT(x);
    return x;
}

static subr(apply)
{
    if (!is(Pair, args)) fatal("too few arguments in: apply");
    oop f = car(args);
    oop a = args; 
    assert(is(Pair, a));
    oop b = getTail(a);
    oop c = cdr(b);
    while (is(Pair, c)) a = b, c = cdr(b = c);			
    assert(is(Pair, a));
    setTail(a, car(b));
    return apply(f, cdr(args), env);
}

static subr(current_environment)
{
    return env;
}

static subr(type_of)
{
    arity1(args, "type-of");
    return newLong(getType(getHead(args)));
}

static subr(print)
{
    while (is(Pair, args)) {
	    print(getHead(args));
	    args = getTail(args);
    }
    return nil;
}

static subr(dump)
{
    while (is(Pair, args)) {
	    dump(getHead(args));
	    args = getTail(args);
    }
    return nil;
}

static subr(format)
{
    arity2(args, "format");
    oop ofmt = car(args);		
    if (!is(String, ofmt)) fatal("format is not a string");
    oop oarg = cadr(args);
    wchar_t *fmt = get(ofmt, String,bits);
    int farg= 0;
    
    union { 
        long_t l;  
        void *p;	
        double d; 
    } arg;
    
    switch (getType(oarg)) {
	    case Undefined:					                    break;
	    case Long:	    arg.l = getLong(oarg);		        break;
	    case Double:	arg.d = getDouble(oarg); ++farg;    break;
	    case String:	arg.p = get(oarg, String,bits);	    break;
	    case Symbol:	arg.p = get(oarg, Symbol,bits);	    break;
	    default:	    arg.p = oarg;			            break;
    }
    size_t size= 100;
    wchar_t *p, *np;
    oop ans= nil;
    if (!(p= malloc(sizeof(wchar_t) * size))) return nil;
    for (;;) {
        // WARNING - wtf casting to wchar_t arg.p
	    int n = farg ? swnprintf(p, size, fmt, (wchar_t)arg.d) : swnprintf(p, size, fmt, (wchar_t)arg.p);
	    if (0 <= n && n < size) {
	        ans = newString(p);
	        free(p);
	        break;
	    }
	    if (n < 0 && errno == EILSEQ) {
	        return nil;
	    }
	    if (n >= 0)	size = n + 1;
	    else size *= 2;
	    if (!(np= realloc(p, sizeof(wchar_t) * size))) {
	        free(p);
	        break;
	    }
	    p= np;
    }
    return ans;
}

static subr(form)
{
    return newForm(car(args), cadr(args));
}

static subr(cons)
{
    oop lhs = car(args);
    oop rhs = cadr(args);
    return cons(lhs, rhs);
}

static subr(pairP)
{
    arity1(args, "pair?");
    return newBool(is(Pair, getHead(args)));
}

static subr(car)
{
    arity1(args, "car");
    return car(getHead(args));
}

static subr(set_car)
{
    arity2(args, "set-car");
    oop arg = getHead(args);				
    if (!is(Pair, arg)) return nil;
    return setHead(arg, getHead(getTail(args)));
}

static subr(cdr)
{
    arity1(args, "cdr");
    return cdr(getHead(args));
}

static subr(set_cdr)
{
    arity2(args, "set-cdr");
    oop arg = getHead(args);				
    if (!is(Pair, arg)) return nil;
    return setTail(arg, getHead(getTail(args)));
}

static subr(symbolP)
{
    arity1(args, "symbol?");
    return newBool(is(Symbol, getHead(args)));
}

static subr(stringP)
{
    arity1(args, "string?");
    return newBool(is(String, getHead(args)));
}

static subr(string)
{
    oop arg = car(args);
    int num = isLong(arg) ? getLong(arg) : 0;
    return _newString(num);
}

static subr(string_length)
{
    arity1(args, "string-length");
    oop arg = getHead(args);
    int len = 0;
    switch (getType(arg)) {
	    case String: len= stringLength(arg); break;
	    case Symbol: len= symbolLength(arg); break;
	    default:	 fatal("string-length: non-String argument: %P", arg);
    }
    return newLong(len);
}

static subr(string_at)
{
    arity2(args, "string-at");
    oop arr = getHead(args);
    oop arg = getHead(getTail(args)); if (!isLong(arg)) return nil;
    int idx = getLong(arg);
    switch (getType(arr)) {
	    case String: if (0 <= idx && idx < stringLength(arr)) return newLong(get(arr, String,bits)[idx]); break;
	    case Symbol: if (0 <= idx && idx < symbolLength(arr)) return newLong(get(arr, Symbol,bits)[idx]); break;
	    default:	 fatal("string-at: non-String argument: %P", arr);
    }
    return nil;
}

static subr(set_string_at)
{
    arity3(args, "set-string-at");
    
    oop arr = getHead(args);			
    if (!is(String, arr)) { 
        fprintf(stderr, "set-string-at: non-string argument: ");  
        fdumpln(stderr, arr);	 
        fatal(0); 
    }

    oop arg = getHead(getTail(args));		
    if (!isLong(arg)) { 
        fprintf(stderr, "set-string-at: non-integer index: ");  
        fdumpln(stderr, arg);  
        fatal(0); 
    }
    
    oop val = getHead(getTail(getTail(args)));	
    if (!isLong(val)) { 
        fprintf(stderr, "set-string-at: non-integer value: ");  
        fdumpln(stderr, val);  
        fatal(0); 
    }

    int idx = getLong(arg);
    if (idx < 0) return nil;
    int len = stringLength(arr);
    if (len <= idx) {
	    if (len < 2) len = 2;
	    while (len <= idx) len *= 2;
	    set(arr, String,bits, (wchar_t *)GC_realloc(get(arr, String,bits), sizeof(wchar_t) * (len + 1)));
	    set(arr, String,size, newLong(len));
    }
    get(arr, String,bits)[idx] = getLong(val);
    return val;
}

static subr(string_copy)	// string from len
{
    oop str = car(args);
    if (!is(String, str)) { 
        fprintf(stderr, "string-copy: non-string argument: ");	
        fdumpln(stderr, str);  
        fatal(0); 
    }
    int ifr = 0;
    int sln = stringLength(str);
    oop ofr = cadr(args);
    if (nil != ofr) {			
        if (!isLong(ofr)) { 
            fprintf(stderr, "string-copy: non-integer start: ");  
            fdumpln(stderr, ofr);	 
            fatal(0); 
        }
	    ifr = getLong(ofr);
	    if (ifr < 0  ) ifr = 0;
	    if (ifr > sln) ifr = sln;	
        assert(ifr >= 0 && ifr <= sln);
	    sln -= ifr;			
        assert(sln >= 0);
    }
    oop oln = caddr(args);
    if (nil != oln) {			
        if (!isLong(oln)) { 
            fprintf(stderr, "string-copy: non-integer length: ");  
            fdumpln(stderr, oln);  
            fatal(0); 
        }
	    int iln = getLong(oln);
	    if (iln < 0)   iln = 0;
	    if (iln > sln) iln = sln;		
        assert(iln >= 0 && ifr + iln <= sln);
	    sln = iln;
    }
    return newStringN(get(str, String,bits) + ifr, sln);
}

static subr(string_compare)	// string substring offset=0 length=strlen(substring)
{
    oop str = car(args);			
    if (!is(String, str)) { 
        fprintf(stderr, "string-compare: non-string argument: ");  
        fdumpln(stderr, str);  
        fatal(0); 
    }
    
    oop arg = cadr(args);			
    if (!is(String, arg)) { 
        fprintf(stderr, "string-compare: non-string argument: ");  
        fdumpln(stderr, arg);  
        fatal(0); 
    }
    
    oop oof = caddr(args);
    int off = 0;
    if (nil != oof) {			
        if (!isLong(oof)) { 
            fprintf(stderr, "string-compare: non-integer offset: ");  
            fdumpln(stderr, oof);  
            fatal(0); 
        }
        off = getLong(oof);
    }
    
    oop oln = car(cdr(cddr(args)));
    int len = stringLength(str);
    if (nil != oln) {			
        if (!isLong(oln)) { 
            fprintf(stderr, "string-compare: non-integer length: ");  
            fdumpln(stderr, oln);  
            fatal(0); 
        }
        len = getLong(oln);
    }
    
    if (off < 0 || len < 0)       return newLong(-1);
    if (off >= stringLength(str)) return newLong(-1);
    return newLong(wcsncmp(get(str, String,bits) + off, get(arg, String,bits), len));
}

static subr(symbol_compare)
{
  arity2(args, "symbol-compare");

  oop str = getHead(args);			
  if (!is(Symbol, str)) { 
      fprintf(stderr, "symbol-compare: non-symbol argument: ");  
      fdumpln(stderr, str);  fatal(0); 
  }
  oop arg = getHead(getTail(args));		
  if (!is(Symbol, arg)) { 
      fprintf(stderr, "symbol-compare: non-symbol argument: ");  
      fdumpln(stderr, arg);  fatal(0); 
  }

  return newLong(wcscmp(get(str, Symbol,bits), get(arg, Symbol,bits)));
}

static subr(string_symbol)
{
    oop arg = car(args);	
    if (is(Symbol, arg))  return arg;  
    if (!is(String, arg)) return nil;
    return intern(get(arg, String,bits));
}

static subr(symbol_string)
{
    oop arg = car(args);
    if (is(String, arg))  return arg;  
    if (!is(Symbol, arg)) return nil;
    return newString(get(arg, Symbol,bits));
}

static subr(long_double)
{
    oop arg = car(args); 
    if (is(Double, arg)) return arg; 
    if (!isLong(arg))    return nil;
    return newDouble(getLong(arg));
}

static subr(string_long)
{
    oop arg = car(args);				
    if (isLong(arg))      return arg;  
    if (!is(String, arg)) return nil;
    return newLong(wcstol(get(arg, String,bits), 0, 0));
}

static subr(double_long)
{
    oop arg = car(args);				
    if (isLong(arg))    return arg;  
    if (!isDouble(arg)) return nil;
    return newLong((long)getDouble(arg));
}

static subr(string_double)
{
    oop arg = car(args);				
    if (is(Double, arg))  return arg;  
    if (!is(String, arg)) return nil;
    return newDouble(wcstod(get(arg, String,bits), 0));
}

static subr(array)
{
    oop arg = car(args);
    int num = isLong(arg) ? getLong(arg) : 0;
    return newArray(num);
}

static subr(arrayP)
{
    return is(Array, car(args)) ? s_t : nil;
}

static subr(array_length)
{
    arity1(args, "array-length");
    oop arg = getHead(args);		
    if (!is(Array, arg)) { 
        fprintf(stderr, "array-length: non-Array argument: ");  
        fdumpln(stderr, arg);  fatal(0); 
    }
    return get(arg, Array,size);
}

static subr(array_at)
{
    arity2(args, "array-at");
    oop arr = getHead(args);
    oop arg = getHead(getTail(args));	
    if (!isLong(arg)) return nil;
    return arrayAt(arr, getLong(arg));
}

static subr(set_array_at)
{
    arity3(args, "set-array-at");
    oop arr = getHead(args);
    oop arg = getHead(getTail(args));		
    if (!isLong(arg)) return nil;
    oop val = getHead(getTail(getTail(args)));
    return arrayAtPut(arr, getLong(arg), val);
}

static subr(insert_array_at)
{
    arity3(args, "insert-array-at");
    oop arr = getHead(args);
    oop arg = getHead(getTail(args));		
    if (!isLong(arg)) return nil;
    oop val = getHead(getTail(getTail(args)));
    return arrayInsert(arr, getLong(arg), val);
}

static subr(data)
{
    oop arg = car(args);
    int num = isLong(arg) ? getLong(arg) : 0;
    return newData(num);
}

static subr(data_length)
{
    arity1(args, "data-length");
    oop arg = getHead(args);		
    if (!is(Data, arg)) { 
        fprintf(stderr, "data-length: non-Data argument: ");  
        fdumpln(stderr, arg);  
        fatal(0); 
    }
    return newLong(GC_size(arg));
}

static void idxtype(oop args, char *who)
{
    fprintf(stderr, "\n%s: non-integer index: ", who);
    fdumpln(stderr, args);
    fatal(0);
}

static void valtype(oop args, char *who)
{
    fprintf(stderr, "\n%s: improper store: ", who);
    fdumpln(stderr, args);
    fatal(0);
}

static inline unsigned long checkRange(oop obj, unsigned long offset, 
                                       unsigned long eltsize, oop args, char *who)
{
    if (isLong(obj)) return getLong(obj) + offset;
    if (offset + eltsize > GC_size(obj)) {
	    fprintf(stderr, "\n%s: index (%ld) out of range: ", who, offset);
	    fdumpln(stderr, args);
	    fatal(0);
    }
    return (unsigned long)obj + offset;
}

#define accessor(name, otype, ctype)											                        \
    static subr(name##_at)												                                \
    {															                                        \
	    oop arg = args;	if (!isPair(arg)) arity(args, #name"-at");		                                \
	    oop obj = getHead(arg);	arg= getTail(arg); if (!isPair(arg)) arity(args, #name"-at");		    \
	    oop idx = getHead(arg);	arg= getTail(arg); if (!isLong(idx)) idxtype(args, #name"-at");		    \
	    unsigned long off = getLong(idx);										                        \
	    if (isPair(arg)) {												                                \
	        oop mul  = getHead(arg); if (!isLong(mul)) idxtype(args, #name"-at");		                \
	        off     *= getLong(mul); if (nil != getTail(arg)) arity(args, #name"-at");	                \
	    } else off *= sizeof(ctype);											                        \
	        return new##otype(*(ctype *)checkRange(obj, off, sizeof(ctype), args, #name"-at"));		    \
    }															                                        \
															                                            \
    static subr(set_##name##_at)											                            \
    {															                                        \
	    oop arg = args; if (!isPair(arg)) arity(args, "set-"#name"-at");	                            \
	    oop obj = getHead(arg);	arg= getTail(arg);	if (!isPair(arg)) arity(args, "set-"#name"-at");	\
	    oop idx = getHead(arg);	arg= getTail(arg);	if (!isPair(arg)) arity(args, "set-"#name"-at");	\
	    oop val = getHead(arg);	arg= getTail(arg);	if (!isLong(idx)) idxtype(args, "set-"#name"-at");	\
	    unsigned long off = getLong(idx);										                        \
	    if (isPair(arg)) {   	                                                                        \
            if (!isLong(val)) idxtype(args, "set-"#name"-at");                                          \
	        off *= getLong(val);											                            \
	        val  = getHead(arg); if (nil != getTail(arg)) arity(args, "set-"#name"-at");	            \
	    } else off *= sizeof(ctype);				                                                    \
        if (!is##otype(val)) valtype(args, "set-"#name"-at");	                                        \
	    *(ctype *)checkRange(obj, off, sizeof(ctype), args, "set-"#name"-at")= get##otype(val);			\
	    return val;													\
    }

accessor(byte, Long, unsigned char)
accessor(int32, Long, int32_t)
accessor(float, Double, float)

#if defined(_MSC_VER)
    #include "windows.h"
#else
    #define __USE_GNU
    #include <dlfcn.h>
    #undef __USE_GNU
#endif

static subr(subr)
{
    oop arg = car(args);
    wchar_t *name= 0;
    switch (getType(arg))
    {
	    case String: name= get(arg, String,bits);  break;
	    case Symbol: name= get(arg, Symbol,bits);  break;
	    default:	 fatal("subr: argument must be string or symbol");
    }

    char    *sym   = wcs2mbs(name);
    HMODULE handle = GetModuleHandle(NULL);
    void    *addr  = GetProcAddress(handle, sym);
    if (!addr) fatal("could not find symbol: %s", sym);
    proto_t *sig   = 0;
    arg            = cadr(args);
    if (nil != arg) {				
        if (!is(String, arg)) { 
            fprintf(stderr, "subr: non-String signature: ");  
            fdumpln(stderr, arg);	 
            fatal(0); 
        }
	    wchar_t	 *spec = get(arg, String,bits);
	    int	     mode  = 0;
	    cast_t	 cast  = 0;
	    ffi_type *type = 0;
	    sig= calloc(1, sizeof(proto_t));
	    sig->arg_count = 0;
	    sig->arg_rest  = 0;
	    while ((mode= *spec++)) {
	        switch (mode) {
		    case 'd': type= &ffi_type_double;	  cast = ffdouble;	break;
		    case 'f': type= &ffi_type_float;	  cast = fffloat;	break;
		    case 'i': type= &ffi_type_sint;	      cast = ffint;		break;
		    case 'j': type= &ffi_type_sint32;	  cast = ffint32;	break;
		    case 'k': type= &ffi_type_sint64;	  cast = ffint64;	break;
		    case 'l': type= &ffi_type_slong;	  cast = fflong;	break;
		    case 'p': type= &ffi_type_pointer;    cast = ffpointer;	break;
		    case 's': type= &ffi_type_pointer;    cast = ffstring;	break;
		    case 'S': type= &ffi_type_pointer;    cast = ffpointer;	break;
		    case '.': sig->arg_rest++;				                break;
		    default:  fatal("illegal type specification: %s", get(arg, String,bits));
	        }
	        if (sig->arg_rest) break;
	        sig->arg_types[sig->arg_count] = type;
	        sig->arg_casts[sig->arg_count] = cast;
	        sig->arg_count++;
	    }
    }
    return newSubr(name, addr, sig);
}

static subr(subr_name)
{
    oop arg = car(args);				
    if (!is(Subr, arg)) { 
        fprintf(stderr, "subr-name: non-Subr argument: ");  
        fdumpln(stderr, arg);	 
        fatal(0); 
    }
    return newString(get(arg, Subr,name));
}

static subr(allocate)
{
    arity2(args, "allocate");
    oop type = getHead(args);			if (!isLong(type)) return nil;
    oop size = getHead(getTail(args));	if (!isLong(size)) return nil;
    return _newOops(getLong(type), sizeof(oop) * getLong(size));
}

static subr(allocate_atomic)
{
    arity2(args, "allocate-atomic");
    oop type = getHead(args);			if (!isLong(type)) return nil;
    oop size = getHead(getTail(args));	if (!isLong(size)) return nil;
    return _newBits(getLong(type), getLong(size));
}

static subr(oop_at)
{
    arity2(args, "oop-at");
    oop obj = getHead(args);
    oop arg = getHead(getTail(args));	if (!isLong(arg)) return nil;
    return oopAt(obj, getLong(arg));
}

static subr(set_oop_at)
{
    arity3(args, "set-oop-at");
    oop obj = getHead(args);
    oop arg = getHead(getTail(args));	if (!isLong(arg)) return nil;
    oop val = getHead(getTail(getTail(args)));
    return oopAtPut(obj, getLong(arg), val);
}

static subr(not)
{
    arity1(args, "not");
    oop obj = getHead(args);
    return (nil == obj) ? s_t : nil;
}

static subr(verbose)
{
    oop obj = car(args);
    if (nil == obj)   return newLong(opt_v);
    if (!isLong(obj)) return nil;
    opt_v = getLong(obj);
    return obj;
}

static subr(optimised)
{
    oop obj = car(args);
    if (nil == obj)   return newLong(opt_O);
    if (!isLong(obj)) return nil;
    opt_O = getLong(obj);
    return obj;
}

static subr(sin)
{
    oop    obj = getHead(args);
    double arg = 0.0;
    if (isDouble(obj))     arg = getDouble(obj);
    else if (isLong (obj)) arg = (double)getLong (obj);
    else {
	    fprintf(stderr, "sin: non-numeric argument: ");
	    fdumpln(stderr, obj);
	    fatal(0);
    }
    return newDouble(sin(arg));
}

static subr(cos)
{
    oop    obj = getHead(args);
    double arg = 0.0;
    if (isDouble(obj))    arg = getDouble(obj);
    else if (isLong(obj)) arg = (double)getLong (obj);
    else {
	    fprintf(stderr, "cos: non-numeric argument: ");
	    fdumpln(stderr, obj);
	    fatal(0);
    }
    return newDouble(cos(arg));
}

static subr(log)
{
    oop    obj = getHead(args);
    double arg = 0.0;
    if (isDouble(obj))    arg =	getDouble(obj);
    else if (isLong(obj)) arg = (double)getLong  (obj);
    else {
	    fprintf(stderr, "log: non-numeric argument: ");
	    fdumpln(stderr, obj);
	    fatal(0);
    }
    return newDouble(log(arg));
}

static subr(address_of)
{
    oop arg = car(args);
    return newLong((long)arg);
}

typedef struct { char *name;  imp_t imp; } subr_ent_t;

// WARNING
static subr_ent_t subr_tab[65536];

static void replFile(FILE *stream, wchar_t *path)
{
    setVar(input, newLong((long)stream));
    beginSource(path);
    oop obj = nil;
    GC_PROTECT(obj);
    for (;;) {
	    if (stream == stdin) {
	        printf("==>");
	        fflush(stdout);
	    }
	    obj = readExpr(stream);
	    if (obj == DONE) break;
	    if (opt_v) {
	        dumpln(obj);
	        fflush(stdout);
	    }
	    obj = expand(obj, getVar(globals));
	    if (opt_g) encode(obj, getVar(globals));
	    obj = eval  (obj, getVar(globals));
	    if ((stream == stdin) || (opt_v > 0)) {
	        printf(" => ");
	        fflush(stdout);
	        dumpln(obj);
	        fflush(stdout);
	    }
	    if (opt_v) {
            #if (!LIB_GC)
	            GC_gcollect();
	            printf("%ld collections, %ld objects, %ld bytes, %4.1f%% fragmentation\n",
		            (long)GC_collections, (long)GC_count_objects(), (long)GC_count_bytes(),
		            GC_count_fragments() * 100.0);
            #endif
	    }
    }						
    GC_UNPROTECT(obj);
    int c = getwc(stream);
    if (WEOF != c) fatal("unexpected character 0x%02x '%c'\n", c, c);
    endSource();
}

static void replPath(wchar_t *path)
{
    FILE *stream = fopen(wcs2mbs(path), "r");
    if (!stream) {
	    int err = errno;
	    fprintf(stderr, "\nerror: ");
	    errno = err;
	    perror(wcs2mbs(path));
	    fatal(0);
    }
    fwide(stream, 1);
    if (fscanf(stream, "#!%*[^\012\015]"));
    replFile(stream, path);
    fclose(stream);
}

static void sigint(int signo)
{
    fatal("\nInterrupt");
}

static subr_ent_t subr_tab[] = {
    #define _do(NAME, OP)					\
    { " "#OP,			        subr_##NAME                 },
    _do_ibinary()
    _do_binary()
    { " -",			            subr_sub                    },
    { " %",			            subr_mod                    },
     _do_relation()
    { " =",			            subr_eq                     },
    { " !=",			        subr_ne                     },
    #undef _do
    { ".if",			        subr_if                     },
    { ".and",			        subr_and                    },
    { ".or",			        subr_or                     },
    { ".set",			        subr_set                    },
    { ".let",			        subr_let                    },
    { ".while",			        subr_while                  },
    { ".quote",			        subr_quote                  },
    { ".lambda",		        subr_lambda                 },
    { ".define",		        subr_define                 },
    { " defined?",		        subr_definedP               },
    { " exit",			        subr_exit                   },
    { " abort",			        subr_abort                  },
    { " open",			        subr_open                   },
    { " close",			        subr_close                  },
    { " getc",			        subr_getc                   },
    { " putc",			        subr_putc                   },
    { " read",			        subr_read                   },
    { " expand",		        subr_expand                 },
    { " eval",			        subr_eval                   },
    { " apply",			        subr_apply                  },
    { " current-environment",	subr_current_environment    },
    { " type-of",		        subr_type_of                },
    { " print",			        subr_print                  },
    { " dump",			        subr_dump                   },
    { " format",		        subr_format                 },
    { " form",			        subr_form                   },
    { " cons",			        subr_cons                   },
    { " pair?",			        subr_pairP                  },
    { " car",			        subr_car                    },
    { " set-car",		        subr_set_car                },
    { " cdr",			        subr_cdr                    },
    { " set-cdr",		        subr_set_cdr                },
    { " symbol?",		        subr_symbolP                },
    { " string?",		        subr_stringP                },
    { " string",		        subr_string                 },
    { " string-length",		    subr_string_length          },
    { " string-at",		        subr_string_at              },
    { " set-string-at",		    subr_set_string_at          },
    { " string-copy",		    subr_string_copy            },
    { " string-compare",	    subr_string_compare         },
    { " symbol->string",	    subr_symbol_string          },
    { " string->symbol",	    subr_string_symbol          },
    { " symbol-compare",	    subr_symbol_compare         },
    { " long->double",	        subr_long_double            },
    { " string->long",	        subr_string_long            },
    { " double->long",	        subr_double_long            },
    { " string->double",	    subr_string_double          },
    { " array",			        subr_array                  },
    { " array?",		        subr_arrayP                 },
    { " array-length",		    subr_array_length           },
    { " array-at",		        subr_array_at               },
    { " set-array-at",		    subr_set_array_at           },
    { " insert-array-at",	    subr_insert_array_at        },
    { " data",			        subr_data                   },
    { " data-length",		    subr_data_length            },
    { " byte-at",		        subr_byte_at                },
    { " set-byte-at",		    subr_set_byte_at            },
    { " int32-at",		        subr_int32_at               },
    { " set-int32-at",		    subr_set_int32_at           },
    { " float-at",		        subr_float_at               },
    { " set-float-at",		    subr_set_float_at           },
    { " subr",			        subr_subr                   },
    { " subr-name",		        subr_subr_name              },
    { " allocate",		        subr_allocate               },
    { " allocate-atomic",	    subr_allocate_atomic        },
    { " oop-at",		        subr_oop_at                 },
    { " set-oop-at",		    subr_set_oop_at             },
    { " not",			        subr_not                    },
    { " verbose",		        subr_verbose                },
    { " optimised",		        subr_optimised              },
    { " sin",			        subr_sin                    },
    { " cos",			        subr_cos                    },
    { " log",			        subr_log                    },    
    { " address-of",		    subr_address_of             },
    { 0,			            0                           }};

int main(int argc, char **argv)
{
    switch (sizeof(long)) {
	    case  4: ffi_type_long= ffi_type_sint32;	break;
	    case  8: ffi_type_long= ffi_type_sint64;	break;
	    case 16: fatal("I cannot run here");		break;
    }

    argv0= argv[0];

    if ((fwide(stdin, 1) <= 0) || (fwide(stdout, -1) >= 0) || (fwide(stderr, -1) >= 0)) {
	    fprintf(stderr, "Cannot set stream widths.\n");
	    return 1;
    }

    if (!setlocale(LC_CTYPE, "")) {
	    fprintf(stderr, "Cannot set the locale.	Verify your LANG, LC_CTYPE, LC_ALL.\n");
	    return 1;
    }

    GC_INIT();

    _globalCache= _newOops(0, sizeof(oop) * GLOBAL_CACHE_SIZE);	GC_add_root(&_globalCache);

    GC_add_root(&symbols);
    GC_add_root(&globals);
    GC_add_root(&globalNamespace);
    GC_add_root(&expanders);
    GC_add_root(&encoders);
    GC_add_root(&evaluators);
    GC_add_root(&applicators);
    GC_add_root(&backtrace);
    GC_add_root(&arguments);
    GC_add_root(&input);
    GC_add_root(&output);

    symbols= newArray(0);

    s_locals		    = intern(L"*locals*");		   GC_add_root(&s_locals		    );
    s_set		        = intern(L"set");			   GC_add_root(&s_set		        );
    s_define		    = intern(L"define");		   GC_add_root(&s_define		    );
    s_let		        = intern(L"let");			   GC_add_root(&s_let		        );
    s_lambda		    = intern(L"lambda");		   GC_add_root(&s_lambda		    );
    s_quote		        = intern(L"quote");			   GC_add_root(&s_quote		        );
    s_quasiquote	    = intern(L"quasiquote");	   GC_add_root(&s_quasiquote	    );
    s_unquote		    = intern(L"unquote");		   GC_add_root(&s_unquote		    );
    s_unquote_splicing	= intern(L"unquote-splicing"); GC_add_root(&s_unquote_splicing	);
    s_t			        = intern(L"t");				   GC_add_root(&s_t		            );
    s_dot		        = intern(L".");				   GC_add_root(&s_dot		        );
    s_etc		        = intern(L"...");			   GC_add_root(&s_etc		        );
    s_bracket		    = intern(L"bracket");		   GC_add_root(&s_bracket		    );
    s_brace		        = intern(L"brace");			   GC_add_root(&s_brace		        );
    s_main		        = intern(L"*main*");		   GC_add_root(&s_main		        );

    oop tmp = nil; GC_PROTECT(tmp);

    globalNamespace = cons(intern(L"*global-namespace*"), nil);
    globalNamespace = setTail(globalNamespace, cons(globalNamespace, nil));

    globals     = define(globalNamespace, intern(L"*globals*"),     globalNamespace);
    expanders   = define(globalNamespace, intern(L"*expanders*"),   nil);
    encoders    = define(globalNamespace, intern(L"*encoders*"),    nil);
    evaluators  = define(globalNamespace, intern(L"*evaluators*"),  nil);
    applicators = define(globalNamespace, intern(L"*applicators*"), nil);

    traceStack  = newArray(32);	GC_add_root(&traceStack);

    backtrace   = define(globalNamespace, intern(L"*backtrace*"),	nil);
    input       = define(globalNamespace, intern(L"*input*"),	    nil);
    output      = define(globalNamespace, intern(L"*output*"),	    nil);

    currentPath = nil;					GC_add_root(&currentPath);
    currentLine = nil;					GC_add_root(&currentLine);
    currentSource =	cons(nil, nil);		GC_add_root(&currentSource);

    {
	    subr_ent_t *ptr;
	    for (ptr = subr_tab;  ptr->name; ++ptr) {
	        wchar_t *name = wcsdup(mbs2wcs(ptr->name + 1));
	        tmp = newSubr(name, ptr->imp, 0);
	        if ('.' == ptr->name[0]) tmp = newFixed(tmp);
	        define(globalNamespace, intern(name), tmp);
	    }
    }

    tmp = nil; GC_UNPROTECT(tmp);
    int repled= 0;

    {
	    tmp= nil;
        GC_PROTECT(tmp);
	    while (--argc) {
	        tmp = cons(nil, tmp);
	        setHead(tmp, newString(mbs2wcs(argv[argc])));
	    }
	    arguments = define(globalNamespace, intern(L"*arguments*"), tmp);
	    tmp = nil;
        GC_UNPROTECT(tmp);
	    signal(SIGINT, sigint);
    }

    setVar(output, newLong((long)stdout));

    while (is(Pair, getVar(arguments))) {
	    oop argl     = getVar(arguments); GC_PROTECT(argl);
	    oop args     = getHead(argl);
	    oop argt     = getTail(argl);
	    wchar_t *arg = get(args, String,bits);
	    if (!wcscmp (arg, L"-v")) { ++opt_v; }
	    else if (!wcscmp (arg, L"-b"))	{ ++opt_b; }
	    else if (!wcscmp (arg, L"-g"))	{ ++opt_g;  opt_p= 0; }
	    else if (!wcscmp (arg, L"-O"))	{ ++opt_O; }
	    else {
	        if (!opt_b) {
	    	replPath(L"boot.l");
	    	opt_b= 1;
	        } else {
	    	    setVar(arguments, argt);
	    	    replPath(arg);
	    	    repled = 1;
	        }
	        argt = getVar(arguments);
	    }
	    setVar(arguments, argt);				
        GC_UNPROTECT(argl);
    }

    if (!repled) {
	    if (!opt_b) replPath(L"boot.l");
	    replFile(stdin, L"<stdin>");
	    printf("\nmorituri te salutant\n");
    }
    return 0;
}