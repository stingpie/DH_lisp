//============================================================================
// Name        : DH_lisp.cpp
// Author      : gabe
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================
/* lisp-cheney.c Lisp with Cheney's copying GC and NaN boxing by Robert A. van Engelen 2022
        - double precision floating point, symbols, strings, lists, proper closures, and macros
        - over 40 built-in Lisp primitives
        - lexically-scoped locals in lambda, let, let*, letrec, letrec*
        - proper tail-recursion, including tail calls through begin, cond, if, let, let*, letrec, letrec*
        - exceptions and error handling with safe return to REPL after an error
        - break with CTRL-C to return to the REPL (compile: lisp.c -DHAVE_SIGNAL_H)
        - REPL with readline (compile: lisp-cheney.c -DHAVE_READLINE_H -lreadline)
        - load Lisp source code files
        - execution tracing to display Lisp evaluation steps
        - Cheney's compacting garbage collector to recycle unused cons pair cells, atoms and strings */

// modified from original 11/30/2025 to fit as an interpreter within dollhouse.


#ifndef DH_LISP_HPP_
#define DH_LISP_HPP_


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>             /* int64_t, uint64_t (or we can use e.g. unsigned long long instead) */
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>


#include "dollhousefile.hpp"

#ifdef HAVE_SIGNAL_H
#include <signal.h>             /* to catch CTRL-C and continue the REPL */
#define BREAK_ON  signal(SIGINT, (void(*)(int))err)
#define BREAK_OFF signal(SIGINT, SIG_IGN)
#else
#define BREAK_ON  (void)0
#define BREAK_OFF (void)0
#endif

#ifdef HAVE_READLINE_H
#include <readline/readline.h>  /* for convenient line editing ... */
#include <readline/history.h>   /* ... and a history of previous Lisp input */
#else
void using_history() { }
#endif

/* floating point output format */
#define FLOAT "%.17lg"

/* DEBUG: always run GC when allocating cells and atoms/strings on the heap */
#ifdef DEBUG
#define ALWAYS_GC 1
#else
#define ALWAYS_GC 0
#endif

#define MAX_GOSUB_RECURSE 10

#ifndef DOLLHOUSE_HPP_
#include "dollhouse.hpp"
#endif


namespace LISP{



/*----------------------------------------------------------------------------*\
 |      LISP EXPRESSION TYPES AND NAN BOXING                                  |
\*----------------------------------------------------------------------------*/

/* we only need three types to implement a Lisp interpreter with a copying garbage collector:
        L      Lisp expression (a double with NaN boxing)
        I      unsigned integer (64 bit unsigned)
        S      signed integer, size of an atom string on the heap or atom forwarding index when negative
   L variables and function parameters are named as follows:
        x,y    any Lisp expression
        n      number
        t,s    list
        f      function or Lisp primitive
        p      pair, a cons of two Lisp expressions
        e,d    environment, a list of pairs, e.g. created with (define v x)
        v      the name of a variable (an atom) or a list of variables
   I variables and function parameters are named as follows:
        i,j,k  any unsigned integer, e.g. a NaN-boxed ordinal value or index
        t      a NaN-boxing tag
   S variables are named as follows:
        n      string length or negative forwarding index of an ATOM/STRING */
typedef double   L;                             /* Lisp expression type: NaN-boxed 64 bit double floating point */
typedef uint64_t I;                             /* unsigned 64 bit integer of a NaN-boxed double */
typedef int      S;                             /* signed size of an atom string on the heap, negative for forwarding */
typedef L       *P;                             /* pointer to a root variable with a value that is updated by GC */

/* T(x) returns the tag bits of a NaN-boxed Lisp expression x */
#define T(x) (*(I*)&x >> 48)

/* primitive, atom, string, pair, closure, macro, GC forward, GC var pointer and nil tags (reserve 0x7ff8 for nan) */
enum { PRIMITIVE=0x7ff9, ATOM=0x7ffa, STRING=0x7ffb, PAIR=0x7ffc, CLOSURE=0x7ffe, MACRO=0x7fff, FORW=0xfffd, VARP=0xfffe, NIL=0xffff };

/* NaN-boxing specific functions */
L box(I t, I i) { i |= t<<48; return *(P)&i; }          /* return NaN-boxed double with tag t and 48 bit ordinal i */
I ord(L x)      { return *(I*)&x & 0xffffffffffff; }    /* remove tag bits to return the 48 bit ordinal */
L num(L n)      { return n; }                           /* check for a valid number: return n == n ? n : err(5); */
I equ(L x, L y) { return *(I*)&x == *(I*)&y; }          /* return nonzero if x equals y */

/*----------------------------------------------------------------------------*\
 |      ERROR HANDLING AND ERROR MESSAGES                                     |
\*----------------------------------------------------------------------------*/

/* state of the setjump-longjmp exception handler with jump buffer jb and number of active root variables n */
struct State {
  jmp_buf jb;
  int n;
} state;

/* report and throw an exception */
#define ERR(n, ...) (fprintf(stderr, __VA_ARGS__), err(n))
L err(int n) { longjmp(state.jb, n); }

#define ERRORS 8
const char *errors[ERRORS+1] = {
  "", "not a pair", "break", "unbound symbol", "cannot apply", "arguments", "stack over", "out of memory", "syntax"
};

/*----------------------------------------------------------------------------*\
 |      MEMORY MANAGEMENT AND RECYCLING                                       |
\*----------------------------------------------------------------------------*/

#define A(lispenv) (char*)lispenv->cell                           /* address of the atom heap */
#define B(lispenv) (char*)lispenv->from                           /* address of the atom "from" heap during garbage collection */
#define W sizeof(S)                             /* width of the size field of an atom string on the heap, in bytes */
//#define N 8192                                  /* heap size */







typedef struct LispEnv{
	/* hp: heap pointer, A+hp with hp=0 points to the first atom string in heap[]
	   sp: stack pointer, the stack starts at the top of the primary heap cell[] with sp=N
	   tr: 0 when tracing is off, 1 or 2 to trace Lisp evaluation steps */
	I hp;
	I sp;
	I tr;
	unsigned int N;
	/* we use two heaps: a primary heap cell[] and a secondary heap for the copying garbage collector */
	L  *cell, *from;
	/* the roots of the garbage collector is a Lisp list of VARP pointers to global and local variables */
	L vars;
	/* Lisp constant expressions () (nil), #t and the global environment env */
	L nil, tru, env;
    //char* main_program;


	L *heap;

	// dollhouse daemon
	Daemon *daemon;
	char yield; // if true, this lisp env wants to yield control.
	Buffer output_buffer;
	char outputName[DH_INTERFACE_NAME_LEN];


	Buffer program_stack[MAX_GOSUB_RECURSE];
	uint16_t prog_idx_stack[MAX_GOSUB_RECURSE];
	uint8_t prog_stack_idx;


	char buf[256], see = '\n', *ptr = "", *line = NULL, ps[20];

	//uint16_t buf_size=0;

//	I fin = 0;
//	FILE *in[10];

}LispEnv;


Buffer output(L, LispEnv*);


LispEnv *NewLispEnvironment(unsigned int size, Daemon *daemon){
	LispEnv *new_environment=(LispEnv*)malloc(sizeof(LispEnv));//+sizeof(L)*2*size);
	new_environment->heap = (L*)calloc(sizeof(L), 2*size);
	new_environment->hp=0;
	new_environment->tr=1;
	new_environment->cell = new_environment->heap;
	new_environment->sp = size;
	new_environment->N = size;
	new_environment->daemon=daemon;
	new_environment->yield = 0;
	new_environment->output_buffer={0, nullptr};
	new_environment->see='\n';
	new_environment->ptr="";
	new_environment->line=NULL;
	new_environment->prog_stack_idx;
	return new_environment;
}


void EraseLispEnvironment(LispEnv *lispenv){
	free(lispenv->heap);
	free(lispenv);
}

void print(L, LispEnv*);

void debugHeapPrint(int idx, int len, LispEnv *lispenv){
	for(int i=0; i<len; i++){
		if(i%64==0) printf("\n%i: ",i+idx);
		printf("%c", A(lispenv)+i+idx);
	}
	printf("\n");
}
void debugHeapPrintType(int idx, int len, LispEnv *lispenv){
	for(int i=0; i<len; i++){
		if(i%64==0) printf("\n%i: ",i+idx);
		printf("%x", T( *((P)(A(lispenv)+i+idx))));
	}
	printf("\n");
}


/* move ATOM/STRING/PAIR/CLOSURE/MACRO/VARP x from the 1st to the 2nd heap or use its forwarding index, return updated x */
L move(L x, LispEnv *lispenv) {
  I t = T(x), i = ord(x);                       /* save the tag and ordinal of x */
  if (t == VARP) {                              /* if x is a VARP */
    *(P)i = move(*(P)i, lispenv);                        /*   update the variable by moving its value to the "to" heap */
    return x;                                   /*   return VARP x */
  }
  if ((t & ~(ATOM^STRING)) == ATOM) {             /* if x is an ATOM or a STRING */
    I j = i-W;                                  /*   j is the index of the size field located before the string */
    S n = *(S*)(B(lispenv)+j);                           /*   get size n of the string at the "from" heap to move */
    if (n < 0)                                  /*   if the size is negative, it is a forwarding index */
      return box(t, -n);                        /*     return ATOM with forwarded index to the location on "to" heap */
    memcpy(A(lispenv)+lispenv->hp, B(lispenv)+j, W+n);                     /*   move the size field and string from the "from" to the "to" heap */
    *(S*)(B(lispenv)+j) = -(S)(W+lispenv->hp);                    /*   leave a negative forwarding index on the "from" heap */
    lispenv->hp += W+n;                                  /*   increment heap pointer by the number of allocated bytes */
    return box(t, lispenv->hp-n);                        /*   return ATOM/STRING with index of the string on the "to" heap */
  }
  if ((t & ~(PAIR^MACRO)) != PAIR)               /* if x is not a PAIR/CLOSURE/MACRO pair */
    return x;                                   /*   return x */
  if (T(lispenv->from[i]) == FORW)                       /* if x is a PAIR/CLOSURE/MACRO with forwarding index on the "from" heap */
    return box(t, ord(lispenv->from[i]));                /*   return x with updated index pointing to "to" heap */
  lispenv->cell[--lispenv->sp] = lispenv->from[i+1];                       /* move PAIR/CLOSURE/MACRO pair from the "from" to the "to" heap */
  lispenv->cell[--lispenv->sp] = lispenv->from[i];
  lispenv->from[i] = box(FORW, lispenv->sp);                      /* leave a forwarding index on the "from" heap */
  return box(t, lispenv->sp);                            /* return PAIR/CLOSURE/MACRO with index to the location on the "to" heap */
}

/* garbage collect with root p, returns (moved) p; p=1 forces garbage collection */
L gc(L p, LispEnv *lispenv) {
  if (lispenv->hp > (lispenv->sp-2)<<3 || equ(p, 1) || ALWAYS_GC) {
    BREAK_OFF;                                  /* do not interrupt GC */
    I i = lispenv->N;                                    /* scan pointer starts at the top of the 2nd heap */
    lispenv->hp = 0;                                     /* heap pointer starts at the bottom of the 2nd heap */
    lispenv->sp = lispenv->N;                                     /* stack pointer starts at the top of the 2nd heap */
    lispenv->from = lispenv->cell;                                /* move cells from the original 1st "from" heap cell[] */
    lispenv->cell = &lispenv->heap[lispenv->N *(lispenv->cell == lispenv->heap)];               /* ... to the 2nd heap, which becomes the 1st "to" heap cell[] */
    lispenv->vars = move(lispenv->vars, lispenv);                          /* move the roots */
    p = move(p, lispenv);                                /* move p */
    while (--i >= lispenv->sp)                           /* while the scan pointer did not pass the stack pointer */
    	lispenv->cell[i] = move(lispenv->cell[i], lispenv);                  /*   move the cell from the "from" heap to the "to" heap */
    BREAK_ON;                                   /* enable interrupt */
    if (lispenv->hp > (lispenv->sp-2)<<3)                         /* if the heap is still full after garbage collection */
      err(7);                                   /*   we ran out of memory */
  }
  return p;
}

/*----------------------------------------------------------------------------*\
 |      LISP EXPRESSION PAIRTRUCTION AND INSPECTION                           |
\*----------------------------------------------------------------------------*/

/* allocate n bytes on the heap, returns NaN-boxed t=ATOM or t=STRING */
L alloc(I t, S n, LispEnv *lispenv) {
  L x = box(t, W+lispenv->hp);                           /* NaN-boxed ATOM or STRING points to bytes after the size field W */
  *(S*)(A(lispenv)+lispenv->hp) = n;                              /* save size n field in front of the to-be-saved string on the heap */
  *(A(lispenv)+W+lispenv->hp) = 0;                                /* make string empty, just in case */
  lispenv->hp += W+n;                                    /* try to allocate W+n bytes on the heap */
  return gc(x, lispenv);                                 /* check if space is allocatable, GC if necessary, returns updated x */
}

/* copy string s to the heap, returns NaN-boxed t=ATOM or t=STRING */
L dup_(I t, const char *s, LispEnv *lispenv) {
  S n = strlen(s)+1;                            /* size of n bytes to allocate, to save the atom string */
  L x = alloc(t, n, lispenv);
  memcpy(A(lispenv)+ord(x), s, n);                       /* save the atom string after the size field on the heap */
  return x;
}

L dup_n(I t, const char *s, S n, LispEnv *lispenv) {
  L x = alloc(t, n+1, lispenv);
  memcpy(A(lispenv)+ord(x), s, n+1);                       /* save the atom string after the size field on the heap */
  return x;
}


/* interning of atom names (Lisp symbols), returns a unique NaN-boxed ATOM */
L atom(const char *s, LispEnv *lispenv) {
  I i = 0;
  while (i < lispenv->hp && strcmp(A(lispenv)+W+i, s))            /* search for a matching atom name on the heap */
    i += W+*(S*)(A(lispenv)+i);
  return i < lispenv->hp ? box(ATOM, W+i) : dup_(ATOM, s, lispenv);/* if found then return ATOM else copy string to the heap */
}

/* store string s on the heap, returns a NaN-boxed STRING with heap offset */
L string(const char *s, LispEnv *lispenv) {
  return dup_(STRING, s, lispenv);                          /* copy string+\0 to the heap, return NaN-boxed STRING */
}

L stringn(const char *s, unsigned int n, LispEnv *lispenv) {
  return dup_n(STRING, s, n, lispenv);                          /* copy string+\0 to the heap, return NaN-boxed STRING */
}

/* construct pair (x . y) returns a NaN-boxed PAIR */
L pair(L x, L y, LispEnv *lispenv) {
  lispenv->cell[--lispenv->sp] = x;                               /* push the car value x, this protects x from getting GC'ed */
  lispenv->cell[--lispenv->sp] = y;                               /* push the cdr value y, this protects y from getting GC'ed */
  return gc(box(PAIR, lispenv->sp), lispenv);                     /* make sure we have enough space for the (next) new cons pair */
}

/* return the car of a pair or ERR if not a pair */
#define FIRST(p, lispenv) lispenv->cell[ord(p)+1]
L first(L p, LispEnv *lispenv) {
  return (T(p)&~(PAIR^MACRO)) == PAIR ? FIRST(p, lispenv) : err(1);
}

/* return the cdr of a pair or ERR if not a pair */
#define NEXT(p, lispenv) lispenv->cell[ord(p)]
L next(L p, LispEnv *lispenv) {
  return (T(p)&~(PAIR^MACRO)) == PAIR ? NEXT(p, lispenv) : err(1);
}

/* construct a pair to add to environment *e, returns the list ((v . x) . *e) */
L env_pair(L v, L x, P e, LispEnv *lispenv) {
  L p = pair(v, x, lispenv);                             /* construct the pair (v . x) first, may trigger GC of *e */
  L ret =pair(p, *e, lispenv);                           /* construct the list ((v . x) . *e) with a GC-updated *e */
  //print(ret, lispenv);
  return ret;
}

/* construct a closure, returns a NaN-boxed CLOSURE */
L closure(L v, L x, P e, LispEnv *lispenv) {
  return box(CLOSURE, ord(env_pair(v, x, equ(*e, lispenv->env) ? &lispenv->nil : e, lispenv)));
}

/* construct a macro, returns a NaN-boxed MACRO */
L macro(L v, L x, LispEnv *lispenv) {
  return box(MACRO, ord(pair(v, x, lispenv)));
}

/* look up a symbol in an environment, return its value or ERR if not found */
L assoc(L v, L e, LispEnv *lispenv) {
  if(strlen(A(lispenv)+ord(v))==0) return lispenv->nil; // empty atoms are nil.

  while (T(e) == PAIR && !equ(v, first(first(e, lispenv), lispenv)))
    e = next(e, lispenv);


  printf("heap @: %i\n", ord(v));
  //if(ord(v)==2850)
  //  debugHeapPrint(0,1<<12, lispenv);
  return T(e) == PAIR ? next(first(e, lispenv), lispenv) : T(v) == ATOM ? ERR(3, "unbound %s ", A(lispenv)+ord(v)) : err(3);
}

/* not(x) is nonzero if x is the Lisp () empty list */
I lisp_not(L x){
  return T(x) == NIL;
}

/* more(t) is nonzero if list t has more than one item */
I more(L t, LispEnv *lispenv) {
  return !lisp_not(t) && !lisp_not(next(t, lispenv));
}

/* register n variables as roots for garbage collection, all but the first should be nil */
void var(int n, LispEnv *lispenv, ...) {
  va_list v;
  for (va_start(v, n); n--; ++state.n)
	  lispenv->vars = pair(box(VARP, (I)va_arg(v, P)), lispenv->vars, lispenv);
  va_end(v);
}

/* release n registered variables */
void unwind(int n, LispEnv *lispenv) {
  state.n -= n;
  while (n--)
	  lispenv->vars = next(lispenv->vars, lispenv);
}

/* release n registered variables and return x */
L return_value(int n, L x, LispEnv *lispenv) {
  unwind(n, lispenv);
  return x;
}

L eval(L, P, LispEnv*), parse(LispEnv*);
void print(L, LispEnv*);

/*----------------------------------------------------------------------------*\
 |      READ                                                                  |
\*----------------------------------------------------------------------------*/

/* the file(s) we are reading or fin=0 when reading from the terminal */
//I fin = 0;
//FILE *in[10];

/* specify an input file to parse and try to open it */
//FILE *input(const char *s, LispEnv *lispenv) {
//  return lispenv->fin <= 9 && (lispenv->in[lispenv->fin] = fopen(s, "r")) ? lispenv->in[lispenv->fin++] : NULL;
//}

/* tokenization buffer, the next character we're looking at, the readline line, prompt and input file */
//char buf[256], see = '\n', *ptr = "", *line = NULL, ps[20];

/* advance to the next character */
void look(LispEnv *lispenv) {

  lispenv->see = lispenv->program_stack[lispenv->prog_stack_idx].data[lispenv->prog_idx_stack[lispenv->prog_stack_idx]++];
  return;
}

//  int c;
//
//  while (lispenv->fin) {                                 /* if reading from a file */
//	lispenv->see = c = getc(lispenv->in[lispenv->fin-1]);                  /* read a character */
//    if (c != EOF)
//      return;
//    fclose(lispenv->in[--(lispenv->fin)]);                          /* if end of file, then close the file and read previous open file */
//    lispenv->see = '\n';                                 /* pretend we see a newline at eof */
//  }
//#ifdef HAVE_READLINE_H
//  if (lispenv->see == '\n') {                            /* if looking at the end of the current readline line */
//    BREAK_OFF;                                  /* disable interrupt to prevent free() without final line = NULL */
//    if (lispenv->line)                                   /* free the old line that was malloc'ed by readline */
//      free(lispenv->line);
//    lispenv->line = NULL;
//    BREAK_ON;                                   /* enable interrupt */
//    while (!(ptr = lispenv->line = readline(lispenv->ps)))        /* read new line and set ptr to start of the line */
//      freopen("/dev/tty", "r", stdin);          /* try again when line is NULL after EOF by CTRL-D */
//    add_history(lispenv->line);                          /* make it part of the history */
//    strcpy(lispenv->ps, "?");                            /* change prompt to ? */
//  }
//  if (!(lispenv->see = *lispenv->ptr++))
//	  lispenv->see = '\n';
//#else
//  if (lispenv->see == '\n') {
//    printf("%s", lispenv->ps);
//    strcpy(lispenv->ps, "?");
//  }
//  if ((c = getchar()) == EOF) {
//    freopen("/dev/tty", "r", stdin);
//    c = '\n';
//  }
//  lispenv->see = c;
//#endif
//}

/* return nonzero if we are looking at character c, ' ' means any white space */
I seeing(char c, LispEnv *lispenv) {
  return c == ' ' ? lispenv->see > 0 && lispenv->see <= c : lispenv->see == c;
}

/* return the look ahead character from standard input, advance to the next */
char get(LispEnv* lispenv) {
  char c = lispenv->see;
  look(lispenv);
  return c;
}

/* tokenize into buf[], return first character of buf[] */
char scan(LispEnv *lispenv) {
  int i = 0;
  while (seeing(' ',lispenv) || seeing(';',lispenv))            /* skip white space and ;-comments */
    if (get(lispenv) == ';')
      while (!seeing('\n',lispenv))                     /* skip ;-comment until newline */
        look(lispenv);
  if (seeing('"',lispenv)) {                            /* tokenize a quoted string */
    do {
      lispenv->buf[i++] = get(lispenv);
      while (seeing('\\',lispenv) && i < sizeof(lispenv->buf)-1) {
        static const char *abtnvfr = "abtnvfr"; /* \a, \b, \t, \n, \v, \f, \r escape codes */
        const char *esc;
        get(lispenv);
        esc = strchr(abtnvfr, lispenv->see);
        lispenv->buf[i++] = esc ? esc-abtnvfr+7 : lispenv->see;   /* replace \x with an escaped code or x itself */
        get(lispenv);
      }
    } while (i <  sizeof(lispenv->buf)-1 && !seeing('"',lispenv) && !seeing('\n',lispenv));
    if (get(lispenv) != '"')
      ERR(8, "missing \" ");
  }
  else if (seeing('(',lispenv) || seeing(')',lispenv) || seeing('\'',lispenv) || seeing('`',lispenv) || seeing(',',lispenv))
	  lispenv->buf[i++] = get(lispenv);                           /* ( ) ' ` , are single-character tokens */
  else                                          /* tokenize a symbol or a number */
    do lispenv->buf[i++] = get(lispenv);
    while (i <  sizeof(lispenv->buf)-1 && !seeing('(',lispenv) && !seeing(')',lispenv) && !seeing(' ',lispenv));
  lispenv->buf[i] = 0;

  return *lispenv->buf;                                  /* return first character of token in buf[] */
}



/* tokenize */
Buffer tokenize(const char* string, Buffer buf) {
  int i = 0;
  char *string_idx;
  while (*string_idx==' ' || *string_idx==';'){ // skip whitespace and comments
	  string_idx++;
	  if (*string_idx==';')
		  string_idx = strchr(string_idx, '\n');
  }
  if (*string_idx == '"'){                            /* tokenize a quoted string */
    do {
      buf.data[i++] = *(string_idx++);
      while ((*string_idx=='\\') && i < buf.size-1) {
        static const char *abtnvfr = "abtnvfr"; /* \a, \b, \t, \n, \v, \f, \r escape codes */
        const char *esc;
        if((esc = strchr(abtnvfr, *(string_idx++)))!=nullptr){
        	buf.data[i++] = esc-abtnvfr+7; // add corresponding escape character to buffer.
        	string_idx++;
        }
        else
        	buf.data[i++] = *(string_idx++); // otherwise, just add the regular character.

      }
    } while (i < buf.size-1 && *string_idx!='"' && *string_idx!='\n');
    if (*(string_idx++) != '"')
      ERR(8, "missing \" ");
  }
  else if (*string_idx=='(' || *string_idx==')' || *string_idx=='\'' || *string_idx=='`' || *string_idx==',')
    buf.data[i++] = *(string_idx++);                   /* ( ) ' ` , are single-character tokens */
  else                                          /* tokenize a symbol or a number */
    do buf.data[i++] =*(string_idx++);
    while (i < buf.size-1 && *string_idx!='(' && *string_idx!=')' && *string_idx!=' ');
  buf.data[i] = 0;
  return buf;
}



/*
L betterreadlisp2(const char* string, LispEnv *lispenv){
	Buffer buffer;
	buffer.size = strlen(string); // tokenized string won't be bigger than original string.
	buffer.data=(char*)calloc(sizeof(char), buffer.size);
	buffer = tokenize(string, buffer);

	for(int i=0; i<buffer.size; i++){
		switch (buffer.data[i]) {
			case '(':  return list(lispenv);                   // if token is ( then parse a list
			//case '\'': x = pair(readlisp(lispenv), lispenv->nil, lispenv);       // construct singleton first, may trigger GC
			//		   return pair(atom("quote", lispenv), x, lispenv);   // if token is ' then quote an expression
			//case '`':  scan(); return bettertick(lispenv);           // if token is a ` then list/quote-convert an expression
			case '"':  return string(buffer.data+1, lispenv);            // if token is a string, then return a new string
			case ')':  return ERR(8, "unexpected ) ");
		  }
		if (sscanf(buf.data, "%lg%n", &x, &i) > 0 && !buf.data[i])
			return x;
	}


}


*/









/* return the Lisp expression parsed and read from input */
L readlisp(LispEnv *lispenv) {
  //printf("%s", lispenv->program_stack[lispenv->prog_stack_idx].data+lispenv->prog_idx_stack[lispenv->prog_stack_idx]);
  printf("%s\n", lispenv->ptr);
  scan(lispenv);
  return parse(lispenv);
}

L betterParse(Buffer, LispEnv*);

L betterreadlisp(const char* string, LispEnv *lispenv){
	Buffer buffer;
	buffer.size = strlen(string); // tokenized string won't be bigger than original string.
	buffer.data=(char*)calloc(sizeof(char), buffer.size);
	buffer = tokenize(string, buffer);
	L return_val = betterParse(buffer, lispenv);
	eraseBuffer(buffer);
	return return_val;
}

/* return a parsed Lisp list */
L list(LispEnv *lispenv) {
  L t = lispenv->nil, p = lispenv->nil, x;
  var(2, lispenv, &t, &p);
  while (scan(lispenv) != ')') {
    if (*lispenv->buf == '.' && !lispenv->buf[1]) {               /* parse list with dot pair ( <expr> ... <expr> . <expr> ) */
      x = readlisp(lispenv);                           /* read expression to replace the last nil at the end of the list */
      if (scan(lispenv) != ')')
        ERR(8, "expecting ) ");
      *(T(p) == PAIR ? &NEXT(p, lispenv) : &t) = x;
      break;
    }
    x = pair(parse(lispenv), lispenv->nil, lispenv);                     /* next parsed expression for the list, construct before using p and t */
    p = *(T(p) == PAIR ? &NEXT(p, lispenv) : &t) = x;     /* p is the cdr or head of the list to replace with rest of the list */
  }
  return return_value(2, t, lispenv);
}


/*
L betterlist(Buffer buffer, LispEnv *lispenv) {
  L t = lispenv->nil, p = lispenv->nil, x;
  var(2, lispenv, &t, &p);
  int i=0;
  while (buffer[i] != ')') {
    if (buffer[i] == '.' && !buf[1]) {               // parse list with dot pair ( <expr> ... <expr> . <expr> )
      x = betterreadlisp(buffer, lispenv);                           // read expression to replace the last nil at the end of the list
      if (buffer[i] != ')')
        ERR(8, "expecting ) ");
      *(T(p) == PAIR ? &NEXT(p, lispenv) : &t) = x;
      break;
    }
    x = pair(betterParse(buffer, lispenv), lispenv->nil, lispenv);                     // next parsed expression for the list, construct before using p and t
    p = *(T(p) == PAIR ? &NEXT(p, lispenv) : &t) = x;     // p is the cdr or head of the list to replace with rest of the list
  }
  return return_value(2, t, lispenv);
}

*/

/* return a list/quote-converted Lisp expression (backquote aka. backtick) */
L tick(LispEnv *lispenv) {
  L t = lispenv->nil, p = lispenv->nil, x;
  if (*lispenv->buf == ',')
    return readlisp(lispenv);                          /* parse and return Lisp expression */
  if (*lispenv->buf != '(') {
    x = pair(parse(lispenv), lispenv->nil, lispenv);                     /* construct singleton first, may trigger GC */
    return pair(atom("quote", lispenv), x, lispenv);              /* parse expression and return (quote <expr>) */
  }
  var(2, lispenv, &t, &p);
  t = p = pair(atom("list", lispenv), lispenv->nil, lispenv);
  while (scan(lispenv) != ')') {
    if (*lispenv->buf == '.' && !lispenv->buf[1]) {               /* tick list with dot pair ( <expr> ... <expr> . <expr> ) */
      x = readlisp(lispenv);                           /* read expression to replace the last nil at the end of the list */
      if (scan(lispenv) != ')')
        ERR(8, "expecing ) ");
      *(T(p) == PAIR ? &NEXT(p,lispenv) : &t) = x;
      break;
    }
    x = pair(tick(lispenv), lispenv->nil, lispenv);                      /* next ticked item for the list, construct before using p */
    p = NEXT(p,lispenv) = x;                             /* p is the cdr to replace it with the rest of the list */
  }
  return return_value(2, t, lispenv);                             /* return (list <expr> ... <expr>) */
}



/*
// return a list/quote-converted Lisp expression (backquote aka. backtick)
L bettertick(Buffer buf, LispEnv *lispenv) {
  L t = lispenv->nil, p = lispenv->nil, x;
  if (*buf.data == ',')
    return readlisp(lispenv);                          // parse and return Lisp expression
  if (*buf.data != '(') {
    x = pair(parse(lispenv), lispenv->nil, lispenv);                     // construct singleton first, may trigger GC
    return pair(atom("quote", lispenv), x, lispenv);              // parse expression and return (quote <expr>)
  }
  var(2, lispenv, &t, &p);
  t = p = pair(atom("list", lispenv), lispenv->nil, lispenv);
  while (scan(buf) != ')') {
    if (*buf.data == '.' && !buf.data[1]) {               // tick list with dot pair ( <expr> ... <expr> . <expr> )
      x = readlisp(lispenv);                           // read expression to replace the last nil at the end of the list
      if (scan() != ')')
        ERR(8, "expecting ) ");
      *(T(p) == PAIR ? &NEXT(p,lispenv) : &t) = x;
      break;
    }
    x = pair(tick(lispenv), lispenv->nil, lispenv);                      // next ticked item for the list, construct before using p
    p = NEXT(p,lispenv) = x;                             // p is the cdr to replace it with the rest of the list
  }
  return return_value(2, t, lispenv);                             // return (list <expr> ... <expr>)
}
*/


/* return a parsed Lisp expression */
L parse(LispEnv *lispenv) {
  L x; int i;
  switch (*lispenv->buf) {
    case '(':  return list(lispenv);                   /* if token is ( then parse a list */
    case '\'': x = pair(readlisp(lispenv), lispenv->nil, lispenv);       /* construct singleton first, may trigger GC */
               return pair(atom("quote", lispenv), x, lispenv);   /* if token is ' then quote an expression */
    case '`':  scan(lispenv); return tick(lispenv);           /* if token is a ` then list/quote-convert an expression */
    case '"':  return string(lispenv->buf+1, lispenv);            /* if token is a string, then return a new string */
    case ')':  return ERR(8, "unexpected ) ");
  }
  if (sscanf(lispenv->buf, "%lg%n", &x, &i) > 0 && !lispenv->buf[i])
    return x;                                   /* return a number, including inf, -inf and nan */
  return atom(lispenv->buf, lispenv);                             /* return an atom (a symbol) */
}


L betterParse(Buffer buf, LispEnv *lispenv){
  L x; int i;
  switch (*buf.data) {
	case '(':  return list(lispenv);                   /* if token is ( then parse a list */
	case '\'': x = pair(readlisp(lispenv), lispenv->nil, lispenv);       /* construct singleton first, may trigger GC */
			   return pair(atom("quote", lispenv), x, lispenv);   /* if token is ' then quote an expression */
	//case '`':  scan(); return bettertick(lispenv);           // if token is a ` then list/quote-convert an expression
	case '"':  return string(buf.data+1, lispenv);            /* if token is a string, then return a new string */
	case ')':  return ERR(8, "unexpected ) ");
  }
  if (sscanf(buf.data, "%lg%n", &x, &i) > 0 && !buf.data[i])
	return x;                                   /* return a number, including inf, -inf and nan */
  return atom(buf.data, lispenv);

}









/*----------------------------------------------------------------------------*\
 |      PRIMITIVEITIVES -- SEE THE TABLE WITH COMMENTS FOR DETAILS                 |
\*----------------------------------------------------------------------------*/

/* the file we are writing to, stdout by default */
FILE *out = stdout;



/* construct a new list of evaluated expressions in list t, i.e. the arguments passed to a function or primitive */
L evlis(P t, P e, LispEnv *lispenv) {
  L s = lispenv->nil, p = lispenv->nil;                           /* new list s = nil with tail pair p = nil */
  var(2, lispenv, &s, &p);                               /* register s and p for GC updates */
  for (; T(*t) == PAIR; *t = next(*t, lispenv)) {         /* iterate over the list of arguments */
    L x = pair(eval(first(*t, lispenv), e, lispenv), lispenv->nil, lispenv);          /* evaluate argument */
    p = *(T(p) == PAIR ? &NEXT(p, lispenv) : &s) = x;     /* build the evaluated list s */
  }
  if (T(*t) != NIL) {                           /* dot list arguments? */
    L x = eval(*t, e, lispenv);                          /* evaluate the dotted argument */
    *(T(p) == PAIR ? &NEXT(p, lispenv) : &s) = x;         /* build the evaluated list s */
  }
  return return_value(2, s, lispenv);                             /* return the list s of evaluated arguments */
}

L f_type(P t, P e, LispEnv *lispenv) {
  L x = first(evlis(t, e, lispenv), lispenv);
  return T(x) == NIL ? -1.0 : T(x) >= PRIMITIVE && T(x) <= MACRO ? T(x) - PRIMITIVE + 1 : 0.0;
}

L f_eval(P t, P e, LispEnv *lispenv) {
  return first(evlis(t, e, lispenv), lispenv);
}

L f_quote(P t, P _, LispEnv *lispenv) {
  return first(*t, lispenv);
}

L f_pair(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv);
  return pair(first(s, lispenv), first(next(s, lispenv), lispenv), lispenv);
}

L f_first(P t, P e, LispEnv *lispenv) {
  return first(first(evlis(t, e, lispenv), lispenv), lispenv);
}

L f_next(P t, P e, LispEnv *lispenv) {
  return next(first(evlis(t, e, lispenv), lispenv), lispenv);
}

L f_add(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), n = first(s, lispenv);
  while (!lisp_not(s = next(s, lispenv)))
    n += first(s, lispenv);
  return num(n);
}

L f_sub(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), n = lisp_not(next(s, lispenv)) ? -first(s, lispenv) : first(s, lispenv);
  while (!lisp_not(s = next(s, lispenv)))
    n -= first(s, lispenv);
  return num(n);
}

L f_mul(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), n = first(s, lispenv);
  while (!lisp_not(s = next(s, lispenv)))
    n *= first(s, lispenv);
  return num(n);
}

L f_div(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), n = lisp_not(next(s, lispenv)) ? 1.0/first(s, lispenv) : first(s, lispenv);
  while (!lisp_not(s = next(s, lispenv)))
    n /= first(s, lispenv);
  return num(n);
}

L f_int(P t, P e, LispEnv *lispenv) {
  L n = first(evlis(t, e, lispenv), lispenv);
  return n < 1e16 && n > -1e16 ? (int64_t)n : n;
}

L f_lt(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), x = first(s, lispenv), y = first(next(s, lispenv), lispenv);
  return (T(x) == T(y) && (T(x) & ~(ATOM^STRING)) == ATOM ? strcmp(A(lispenv)+ord(x), A(lispenv)+ord(y)) < 0 :
      x == x && y == y ? x < y :
      T(x) < T(y)) ? lispenv->tru : lispenv->nil;
}

L f_eq(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), x = first(s, lispenv), y = first(next(s, lispenv), lispenv);
  return (T(x) == STRING && T(y) == STRING ? !strcmp(A(lispenv)+ord(x), A(lispenv)+ord(y)) : equ(x, y)) ? lispenv->tru : lispenv->nil;
}

L f_not(P t, P e, LispEnv *lispenv) {
  return lisp_not( first(evlis(t, e, lispenv), lispenv)) ? lispenv->tru : lispenv->nil;;
}

L f_or(P t, P e, LispEnv *lispenv) {
  L x = lispenv->nil;
  while (T(*t) != NIL && lisp_not(x = eval(first(*t, lispenv), e, lispenv)))
    *t = next(*t, lispenv);
  return x;
}

L f_and(P t, P e, LispEnv *lispenv) {
  L x = lispenv->tru;
  while (T(*t) != NIL && !lisp_not(x = eval(first(*t, lispenv), e, lispenv)))
    *t = next(*t, lispenv);
  return x;
}

L f_list(P t, P e, LispEnv *lispenv) {
  return evlis(t, e, lispenv);
}

L f_begin(P t, P e, LispEnv *lispenv) {
  for (; more(*t, lispenv); *t = next(*t, lispenv))
    eval(first(*t, lispenv), e, lispenv);
  return T(*t) == NIL ? lispenv->nil : first(*t, lispenv);
}

L f_while(P t, P e, LispEnv *lispenv) {
  L s = lispenv->nil, x = lispenv->nil;
  var(2, lispenv, &s, &x);
  while (!lisp_not(eval(first(*t, lispenv), e, lispenv)))
    for (s = next(*t, lispenv); T(s) != NIL; s = next(s, lispenv))
      x = eval(first(s, lispenv), e, lispenv);
  return return_value(2, x, lispenv);
}

L f_cond(P t, P e, LispEnv *lispenv) {
  while (T(*t) != NIL && lisp_not(eval(first(first(*t, lispenv), lispenv), e, lispenv)))
    *t = next(*t, lispenv);
  if (T(*t) != NIL)
    *t = next(first(*t, lispenv), lispenv);
  return f_begin(t, e, lispenv);
}

L f_if(P t, P e, LispEnv *lispenv) {
  return lisp_not(eval(first(*t, lispenv), e, lispenv)) ? (*t = next(next(*t, lispenv), lispenv), f_begin(t, e, lispenv)) : first(next(*t, lispenv), lispenv);
}

L f_lambda(P t, P e, LispEnv *lispenv) {
  return closure(first(*t, lispenv), first(next(*t, lispenv), lispenv), e, lispenv);
}

L f_macro(P t, P e, LispEnv *lispenv) {
  return macro(first(*t, lispenv), first(next(*t, lispenv), lispenv), lispenv);
}

L f_define(P t, P e, LispEnv *lispenv) {
  L x = eval(first(next(*t, lispenv), lispenv), e, lispenv), v = first(*t, lispenv), d;

  for (d = *e; T(d) == PAIR && !equ(v, first(first(d, lispenv), lispenv)); d = next(d, lispenv))
    continue;
  if (T(d) == PAIR)
    NEXT(first(d, lispenv), lispenv) = x;
  else
	lispenv->env = env_pair(v, x, &lispenv->env, lispenv);

  print(lispenv->env, lispenv);

  return first(*t, lispenv);
}

L f_assoc(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv);
  return assoc(first(s, lispenv), first(next(s, lispenv), lispenv), lispenv);
}

L f_env(P _, P e, LispEnv *lispenv) {
  return *e;
}

L f_let(P t, P e, LispEnv *lispenv) {
  L d = *e, x = lispenv->nil;
  var(2, lispenv, &d, &x);
  for (; more(*t, lispenv); *t = next(*t, lispenv)) {
    x = next(first(*t, lispenv), lispenv);
    x = eval(f_begin(&x, e, lispenv), &d, lispenv);
    *e = env_pair(first(first(*t, lispenv), lispenv), x, e, lispenv);
  }
  return return_value(2, first(*t, lispenv), lispenv);
}

L f_leta(P t, P e, LispEnv *lispenv) {
  L s = lispenv->nil, x;
  var(1, lispenv, &s);
  for (; more(*t, lispenv); *t = next(*t, lispenv)) {
    s = next(first(*t, lispenv), lispenv);
    x = eval(f_begin(&s, e, lispenv), e, lispenv);
    *e = env_pair(first(first(*t, lispenv), lispenv), x, e, lispenv);
  }
  return return_value(1, first(*t, lispenv), lispenv);
}

L f_letrec(P t, P e, LispEnv *lispenv) {
  L s = lispenv->nil, x = lispenv->nil;
  var(2, lispenv, &s, &x);
  for (s = *t; more(s, lispenv); s = next(s, lispenv))
    *e = env_pair(first(first(s, lispenv), lispenv), lispenv->nil, e, lispenv);
  for (s = *e; more(*t, lispenv); s = next(s, lispenv), *t = next(*t, lispenv)) {
    x = next(first(*t, lispenv), lispenv);
    x = eval(f_begin(&x, e, lispenv), e, lispenv);
    NEXT(first(s,lispenv),lispenv) = x;
  }
  return return_value(2, T(*t) == NIL ? lispenv->nil : first(*t, lispenv), lispenv);
}

L f_letreca(P t, P e, LispEnv *lispenv) {
  L s = lispenv->nil, x;
  var(1, lispenv, &s);
  for (; more(*t, lispenv); *t = next(*t, lispenv)) {
    *e = env_pair(first(first(*t, lispenv), lispenv), lispenv->nil, e, lispenv);
    s = next(first(*t, lispenv), lispenv);
    x = eval(f_begin(&s, e, lispenv), e, lispenv);
    NEXT(first(*e,lispenv),lispenv) = x;
  }
  return return_value(1, T(*t) == NIL ? lispenv->nil : first(*t, lispenv), lispenv);
}

L f_setq(P t, P e, LispEnv *lispenv) {
  L x = eval(first(next(*t, lispenv), lispenv), e, lispenv), v = first(*t, lispenv), d;
  for (d = *e; T(d) == PAIR && !equ(v, first(first(d, lispenv), lispenv)); d = next(d, lispenv))
    continue;
  return T(d) == PAIR ? NEXT(first(d,lispenv),lispenv) = x : T(v) == ATOM ? ERR(3, "unbound %s ", A(lispenv)+ord(v)) : err(3);
}

L f_setfirst(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), p = first(s, lispenv);
  return (T(p) == PAIR) ? FIRST(p,lispenv) = first(next(s,lispenv),lispenv) : err(1);
}

L f_setnext(P t, P e, LispEnv *lispenv) {
  L s = evlis(t, e, lispenv), p = first(s, lispenv);
  return (T(p) == PAIR) ? NEXT(p,lispenv) = first(next(s,lispenv),lispenv) : err(1);
}



L f_print(P t, P e, LispEnv *lispenv) {
  L s;
  for (s = evlis(t, e, lispenv); T(s) != NIL; s = next(s, lispenv))
    print(first(s, lispenv), lispenv);
  return lispenv->nil;
}

L f_println(P t, P e, LispEnv *lispenv) {
  f_print(t, e, lispenv);
  putc('\n', out);
  return lispenv->nil;
}

L f_write(P t, P e, LispEnv *lispenv) {
  L s;
  for (s = evlis(t, e, lispenv); T(s) != NIL; s = next(s, lispenv)) {
    L x = first(s, lispenv);
    if (T(x) == STRING)
      fprintf(out, "%s", A(lispenv)+ord(x));
    else
      print(x, lispenv);
  }
  return lispenv->nil;
}

L f_string(P t, P e, LispEnv *lispenv) {
  L s, x; S n;
  for (n = 0, s = *t = evlis(t, e, lispenv); T(s) != NIL; s = next(s, lispenv)) {
    L y = first(s, lispenv);
    if ((T(y) & ~(ATOM^STRING)) == ATOM)
      n += strlen(A(lispenv)+ord(y));
    else if (T(y) == PAIR)
      for (; T(y) == PAIR; y = next(y, lispenv))
        ++n;
    else if (y == y)
      n += snprintf(lispenv->buf, sizeof(lispenv->buf), FLOAT, y);
  }
  x = alloc(STRING, n+1, lispenv);
  n = ord(x);
  for (s = *t; T(s) != NIL; s = next(s, lispenv)) {
    L y = first(s, lispenv);
    if ((T(y) & ~(ATOM^STRING)) == ATOM)
      n += strlen(strcpy(A(lispenv)+n, A(lispenv)+ord(y)));
    else if (T(y) == PAIR)
      for (; T(y) == PAIR; y = next(y, lispenv))
        *(A(lispenv)+n++) = first(y, lispenv);
    else if (y == y)
      n += snprintf(A(lispenv)+n, sizeof(lispenv->buf), FLOAT, y);
  }
  *(A(lispenv)+n) = 0;
  return x;
}

//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
//TODO: ###################################################################################################################################################
void gosub(P t, P e, LispEnv *lispenv) {
  if(lispenv->prog_stack_idx<MAX_GOSUB_RECURSE){
	  L x =f_string(t, e, lispenv);
	  //eraseBuffer(lispenv->program_stack);
	  lispenv->prog_stack_idx++;
	  lispenv->program_stack[lispenv->prog_stack_idx].size=strlen(A(lispenv)+ord(x))+strlen("(eval\n") + strlen("\n)");


	  lispenv->program_stack[lispenv->prog_stack_idx].data=(char*)calloc(sizeof(char),lispenv->program_stack[lispenv->prog_stack_idx].size);
	  memset(lispenv->program_stack[lispenv->prog_stack_idx].data,  '\0', lispenv->program_stack[lispenv->prog_stack_idx].size);

	  //basically, a macro.
	  strcpy(lispenv->program_stack[lispenv->prog_stack_idx].data,"(eval\n");
	  strcpy(strchr(lispenv->program_stack[lispenv->prog_stack_idx].data,'\0'), A(lispenv)+ord(x));
	  strcpy(strchr(lispenv->program_stack[lispenv->prog_stack_idx].data,'\0'), "\n)");

	  printf("%s\n", lispenv->program_stack[lispenv->prog_stack_idx].data);

	  lispenv->prog_idx_stack[lispenv->prog_stack_idx]=0;

	  L ast = readlisp(lispenv);

	  //var(1, lispenv, ast);

	  L ret = eval(ast, e, lispenv);

	  free(lispenv->program_stack[lispenv->prog_stack_idx].data);
	  lispenv->prog_stack_idx--;



	  //unwind(1, lispenv);

  }

}

L f_gosub(P t, P e, LispEnv *lispenv){
	gosub(t, e, lispenv);
	//unwind(1, lispenv);
	return lispenv->nil;
}


// read data from file.
L f_read(P t, P e, LispEnv *lispenv){

  L x =f_string(t, e, lispenv);
  Buffer data = DH_read(A(lispenv)+ord(x));
  // add null terminator.
  data.data = (char*)realloc(data.data, data.size+1);
  data.data[data.size++]='\0';
  //printf("Data: %s Size: %i",(char*)data.data, data.size);
  if(data.size>0)
	  return dup_n(ATOM, (char*)data.data, data.size, lispenv);
  return lispenv->nil;
}


/*
L f_load(P t, P e, LispEnv *lispenv) {
  L x = f_string(t, e, lispenv);
  return input(A(lispenv)+ord(x)) ? x : ERR(5, "cannot open %s ", A(lispenv)+ord(x));
}
*/

/*
L f_token(P t, P e, LispEnv *lispenv){
	L x =f_string(t, e, lispenv);
	return readlisp(A(lispenv)+ord(x), lispenv);
}*/

L f_trace(P t, P e, LispEnv *lispenv) {
  I savedtr = lispenv->tr;
  lispenv->tr = T(*t) == NIL ? 1 : first(*t, lispenv);
  return more(*t, lispenv) ? *t = eval(first(next(*t, lispenv), lispenv), e, lispenv), lispenv->tr = savedtr, *t : lispenv->tr;
}

L f_catch(P t, P e, LispEnv *lispenv) {
  L x;
  struct State saved = state;
  if (!(x = setjmp(state.jb)))
    x = eval(first(*t, lispenv), e, lispenv);
  else {
    unwind(state.n-saved.n, lispenv);
    x = pair(atom("ERR", lispenv), x, lispenv);
  }
  state = saved;
  return x;
}

L f_throw(P t, P e, LispEnv *lispenv) {
  longjmp(state.jb, num(first(*t, lispenv)));
}

L f_quit(P t, P _, LispEnv *lispenv) {
  exit(0);
}

// (reg-interface <name> <type> <format> <closure>)
// If direction==1, closure must accept DH_messages.
// if direction==0, closure must output DH_messages.
// can be called by dollhouse by ( <name> <message details>)
L f_register_interface(P t, P e, LispEnv *lispenv){

	L interface_closure = first(next(next(next(*t,lispenv),lispenv),lispenv), lispenv);

	if(T(interface_closure)!=CLOSURE) return lispenv->nil;


	char *interface_name =	A(lispenv)+ord(f_string(&FIRST(*t, lispenv), e, lispenv));

	char *interface_type = 	A(lispenv)+ord(f_string(&FIRST(next(*t, lispenv), lispenv), e, lispenv));
	char *interface_format =A(lispenv)+ord(f_string(&FIRST(next(next(*t,lispenv),lispenv), lispenv), e, lispenv));

	uint8_t direction = ord(first(next(next(next(next(*t,lispenv),lispenv),lispenv),lispenv), lispenv));
	uint8_t triggering = ord(first(next(next(next(next(next(*t,lispenv),lispenv),lispenv),lispenv),lispenv), lispenv));
	Interface *new_interface=(Interface*)malloc(sizeof(Interface));       // FREE INTERFACES UPON EXIT
	strncpy(new_interface->name, interface_name, DH_INTERFACE_NAME_LEN);
	strncpy(new_interface->type, interface_type, DH_TYPE_LEN);
	strncpy(new_interface->format, interface_format, DH_FORMAT_LEN);
	new_interface->direction = direction;
	new_interface->triggering=triggering;
	new_interface->daemon = lispenv->daemon;
	registerDaemonInterface(new_interface);

	pair(interface_closure, first(*t, lispenv), lispenv);
	return f_define( lispenv->cell-2, e, lispenv);
}



L f_evoke(P t, P e, LispEnv *lispenv){
	L filename_idx = f_string(&FIRST(*t, lispenv), e, lispenv);
	L language_idx = f_string(&NEXT(*t, lispenv), e, lispenv);

	char filename[DH_DAEMON_NAME_LEN];
	char language[DH_LANG_LEN];

	strncpy(filename, A(lispenv) + ord(filename_idx), DH_DAEMON_NAME_LEN);
	strncpy(language, A(lispenv) + ord(language_idx), DH_LANG_LEN);

	return box( ATOM, startDaemon(filename, language)); // return 1 if successful
}


L f_yield(P t, P e, LispEnv *lispenv){
	lispenv->yield=1;
	return lispenv->nil;
}



// t is a pair (<name>, <data>) where <name> contains the interface name, and <data> contains the actual info.
// returns the number of bytes outputed.
L f_output(P t, P e, LispEnv *lispenv){

	L name = f_string(&FIRST(*t, lispenv), e, lispenv);

	// check if the interface exists
	uint8_t isInterface=false;
	for(int i=0; i<lispenv->daemon->interface_num; i++){
		if(strcmp(lispenv->daemon->interfaces[i].name, A(lispenv)+ord(name))){
			isInterface=true;
			break;
		}
	}
	if(!isInterface) return box(ATOM, 0); // return 0 if the interface does not exist

	Buffer newbuffer; // will be freed by cycleInterface
	if(T(NEXT(*t, lispenv)) == STRING ){ // the output is a simple string
		L data = f_string(&NEXT(*t, lispenv), e, lispenv);
		newbuffer.size = strlen(A(lispenv)+ord(data));
		strncpy(newbuffer.data, A(lispenv)+ord(data), newbuffer.size);
		strncpy(lispenv->outputName, A(lispenv)+ord(name), DH_INTERFACE_NAME_LEN);
		memcpy(&(lispenv->output_buffer), &newbuffer, sizeof(Buffer));
	}else if(T(NEXT(*t, lispenv))==PAIR) {

		L data = NEXT(*t, lispenv);
		// get length of buffer needed
		int count=0;
		L cell = data;
		while((cell = next(cell, lispenv))!=lispenv->nil) count++;
		newbuffer.size = count+1;


		count=0;
		while((data = next(data, lispenv))!=lispenv->nil){
			newbuffer.data[count]=ord(first(data, lispenv));
			count++;
		}
		strncpy(lispenv->outputName, A(lispenv)+ord(name), DH_INTERFACE_NAME_LEN);
		memcpy(&(lispenv->output_buffer), &newbuffer, sizeof(Buffer));
	}

	return box(ATOM, newbuffer.size);
}

#define LISP_INPUT_BUFFER_SIZE 1024
L f_input(P t, P e, LispEnv *lispenv){
	char *input_buffer=(char*)malloc(LISP_INPUT_BUFFER_SIZE);
	char input_char=' ';
	uint16_t idx=0;
	while((input_char=getc(stdin))!='\n' && idx<LISP_INPUT_BUFFER_SIZE){
		input_buffer[idx++]=input_char;
	}
	input_buffer[idx++]='\0';
	L x = string(input_buffer, lispenv);
	free(input_buffer);
	return x;
}





/* table of Lisp primitives, each has a name s, a function pointer f, and a tail-recursive flag t */
struct {
  const char *s;
  L (*f)(P, P, LispEnv*);
  short t;
} primitives[] = {
  {"type",     f_type,    0},                   /* (type x) => <type> value between -1 and 7 */
  {"eval",     f_eval,    1},                   /* (eval <quoted-expr>) => <value-of-expr> */
  {"quote",    f_quote,   0},                   /* (quote <expr>) => <expr> -- protect <expr> from evaluation */
  {"pair",     f_pair,    0},                   /* (pair x y) => (x . y) -- construct a pair */
  {"first",    f_first,   0},                   /* (first <pair>) => x -- "deconstruct" <pair> (x . y) */
  {"next",     f_next,    0},                   /* (next <pair>) => y -- "deconstruct" <pair> (x . y) */
  {"+",        f_add,     0},                   /* (+ n1 n2 ... nk) => n1+n2+...+nk */
  {"-",        f_sub,     0},                   /* (- n1 n2 ... nk) => n1-n2-...-nk or -n1 if k=1 */
  {"*",        f_mul,     0},                   /* (* n1 n2 ... nk) => n1*n2*...*nk */
  {"/",        f_div,     0},                   /* (/ n1 n2 ... nk) => n1/n2/.../nk or 1/n1 if k=1 */
  {"int",      f_int,     0},                   /* (int <integer.frac>) => <integer> */
  {"<",        f_lt,      0},                   /* (< n1 n2) => #t if n1<n2 else () */
  {"eq?",      f_eq,      0},                   /* (eq? x y) => #t if x==y else () */
  {"not",      f_not,     0},                   /* (not x) => #t if x==() else ()t */
  {"or",       f_or,      0},                   /* (or x1 x2 ... xk) => #t if any x1 is not () else () */
  {"and",      f_and,     0},                   /* (and x1 x2 ... xk) => #t if all x1 are not () else () */
  {"list",     f_list,    0},                   /* (list x1 x2 ... xk) => (x1 x2 ... xk) -- evaluates x1, x2 ... xk */
  {"begin",    f_begin,   1},                   /* (begin x1 x2 ... xk) => xk -- evaluates x1, x2 to xk */
  {"while",    f_while,   0},                   /* (while x y1 y2 ... yk) -- while x is not () evaluate y1, y2 ... yk */
  {"cond",     f_cond,    1},                   /* (cond (x1 y1) (x2 y2) ... (xk yk)) => yi for first xi!=() */
  {"if",       f_if,      1},                   /* (if x y z) => if x!=() then y else z */
  {"lambda",   f_lambda,  0},                   /* (lambda <parameters> <expr>) => {closure} */
  {"macro",    f_macro,   0},                   /* (macro <parameters> <expr>) => [macro] */
  {"define",   f_define,  0},                   /* (define <symbol> <expr>) -- globally defines <symbol> */
  {"assoc",    f_assoc,   0},                   /* (assoc <quoted-symbol> <environment>) => <value-of-symbol> */
  {"env",      f_env,     0},                   /* (env) => <environment> */
  {"let",      f_let,     1},                   /* (let (v1 x1) (v2 x2) ... (vk xk) y) => y with scope of bindings */
  {"let*",     f_leta,    1},                   /* (let* (v1 x1) (v2 x2) ... (vk xk) y) => y with scope of bindings */
  {"letrec",   f_letrec,  1},                   /* (letrec (v1 x1) (v2 x2) ... (vk xk) y) => y with recursive scope */
  {"letrec*",  f_letreca, 1},                   /* (letrec* (v1 x1) (v2 x2) ... (vk xk) y) => y with recursive scope */
  {"setq",     f_setq,    0},                   /* (setq <symbol> x) -- changes value of <symbol> in scope to x */
  {"set-first!",f_setfirst,0},                   /* (set-car! <pair> x) -- changes car of <pair> to x in memory */
  {"set-next!",f_setnext,  0},                   /* (set-cdr! <pair> y) -- changes cdr of <pair> to y in memory */
  {"read",     f_read,    0},                   /* (read <filename> ) => reads from file */
  {"print",    f_print,   0},                   /* (print x1 x2 ... xk) => () -- prints the values x1 x2 ... xk */
  {"println",  f_println, 0},                   /* (println x1 x2 ... xk) => () -- prints with newline */
  {"write",    f_write,   0},                   /* (write x1 x2 ... xk) => () -- prints without quoting strings */
  {"string",   f_string,  0},                   /* (string x1 x2 ... xk) => <string> -- string of x1 x2 ... xk */
//  {"load",     f_load,    0},                   /* (load <name>) -- loads file <name> (an atom or string name) */
  {"gosub",	   f_gosub,	  1},			// Enter a subroutine
//  {"return",   f_return,  0},
  {"trace",    f_trace,   0},                   /* (trace flag [<expr>]) -- flag 0=off, 1=on, 2=keypress */
  {"catch",    f_catch,   0},                   /* (catch <expr>) => <value-of-expr> if no exception else (ERR . n) */
  {"throw",    f_throw,   0},                   /* (throw n) -- raise exception error code n (integer != 0) */
  {"quit",     f_quit,    0},                   /* (quit) -- bye! */
  {"yield",	   f_yield,   0},					// return execution to the caller.
  {"output",   f_output,  0},                   // (output name data) output <data> to interface <name>
  {"input",	   f_input,   0},
  {0}};








/* evaluate x in environment e, returns value of x, tail-call optimized */
L step(L x, P e, LispEnv *lispenv) {
  L f = lispenv->nil, v = lispenv->nil, d = lispenv->nil, z = lispenv->nil;
  var(5, lispenv, &x, &f, &v, &d, &z);
  while (1) {
	//printf("prog_index: %i\n", lispenv->prog_idx);
    if (T(x) == ATOM)
      return return_value(5, assoc(x, *e, lispenv), lispenv);
    if (T(x) != PAIR)
      return return_value(5, x, lispenv);


    f = eval(first(x, lispenv), e, lispenv);
    x = next(x, lispenv);
    z = *e;
    e = &z;
    //debugHeapPrintType(ord(x),1, lispenv);

    if (T(f) == PRIMITIVE) {
      x = primitives[ord(f)].f(&x, e, lispenv);
      if (!primitives[ord(f)].t)
        return return_value(5, x, lispenv);
    }
    else if (T(f) == CLOSURE) {
      v = first(first(f, lispenv), lispenv);
      d = next(f, lispenv);
      if (T(d) == NIL)
        d = lispenv->env;
      for (; T(v) == PAIR && T(x) == PAIR; v = next(v, lispenv), x = next(x, lispenv)) {
        L y = eval(first(x, lispenv), e, lispenv);
        d = env_pair(first(v, lispenv), y, &d, lispenv);
      }
      if (T(v) == PAIR) {
        x = eval(x, e, lispenv);
        for (; T(v) == PAIR && T(x) == PAIR; v = next(v, lispenv), x = next(x, lispenv))
          d = env_pair(first(v, lispenv), first(x, lispenv), &d, lispenv);
        if (T(v) == PAIR)
          return return_value(5, err(5), lispenv);
      }
      else if (T(x) == PAIR)
        x = evlis(&x, e, lispenv);
      else if (T(x) != NIL)
        x = eval(x, e, lispenv);
      if (T(v) != NIL)
        d = env_pair(v, x, &d, lispenv);
      x = next(first(f, lispenv), lispenv);
      e = &d;
    }
    else if (T(f) == MACRO) {
      d = lispenv->env;
      v = first(f, lispenv);
      for (; T(v) == PAIR && T(x) == PAIR; v = next(v, lispenv), x = next(x, lispenv))
        d = env_pair(first(v, lispenv), first(x, lispenv), &d, lispenv);
      if (T(v) == PAIR)
        return return_value(5, err(5), lispenv);
      if (T(v) != NIL)
        d = env_pair(v, x, &d, lispenv);
      x = eval(next(f, lispenv), &d, lispenv);
    }
    else
      return return_value(5, err(4), lispenv);
  }
}

/* trace the evaluation of x in environment e, returns its value */
L eval(L x, P e, LispEnv *lispenv) {
  L y;
  if (!lispenv->tr)
    return step(x, e, lispenv);
  var(1, lispenv, &x);                                   /* register var x to display later again */
  y = step(x, e, lispenv);

  //if(lispenv->tr>1) printf("X: %i str: %s\n",ord(x), A(lispenv)+ord(x));
  //if(lispenv->tr>1) printf("Y: %i str: %s\n",ord(y), A(lispenv)+ord(y));
  printf("\e[32m%4d: \e[33m", state.n); print(x, lispenv);       /* <vars>: unevaluated expression */
  printf("\e[36m => \e[33m");           print(y, lispenv);       /* => value of the expression */
  //debugHeapPrint(ord(y)-20,40, lispenv);

  printf("\e[m\t");
  if (lispenv->tr > 1)                                   /* wait for ENTER key or other CTRL */
    while (getchar() >= ' ')
      continue;
  else
    putchar('\n');
  return return_value(1, y, lispenv);
}

/*----------------------------------------------------------------------------*\
 |      PRINT                                                                 |
\*----------------------------------------------------------------------------*/

/* output Lisp list t */
void printlist(L t, LispEnv *lispenv) {
  putc('(', out);
  while (1) {
    print(first(t, lispenv), lispenv);
    if (lisp_not(t = next(t, lispenv)))
      break;
    if (T(t) != PAIR) {
      fprintf(out, " . ");
      print(t, lispenv);
      break;
    }
    putc(' ', out);
  }
  putc(')', out);
}



/* output Lisp expression x */
void print(L x, LispEnv *lispenv) {
  switch (T(x)) {
    case NIL:  	  fprintf(out, "()");                   	break;
    case PRIMITIVE:fprintf(out, "<%s>", primitives[ord(x)].s); 	break;
    case ATOM: 	  fprintf(out, "%s", A(lispenv)+ord(x));         	break;
    case STRING:  fprintf(out, "\"%s\"", A(lispenv)+ord(x));     	break;
    case PAIR: 	  printlist(x, lispenv);                         	break;
    case CLOSURE: fprintf(out, "{%lu}", ord(x));       	break;
    case MACRO:   fprintf(out, "[%lu]", ord(x));       	break;
    default:   	  fprintf(out, FLOAT, x);               	break;
  }
}

/*----------------------------------------------------------------------------*\
 |      REPL                                                                  |
\*----------------------------------------------------------------------------*/

/* entry point with Lisp initialization, error handling and REPL *//*
int main(int argc, char **argv) {
  int i;

  LispEnv *lispenv = NewLispEnvironment(8192);

  printf("lisp");
  input(argc > 1 ? argv[1] : "init.lisp");      // set input source to load when available
  out = stdout;
  if (setjmp(state.jb))                         // if something goes wrong before REPL, it is fatal
    abort();
  lispenv->vars = lispenv->nil = box(NIL, 0);
  lispenv->tru = atom("#t", lispenv);
  var(1, lispenv, &lispenv->tru);                                 // make tru a root var
  lispenv->env = env_pair(lispenv->tru, lispenv->tru, &lispenv->nil, lispenv);                   // create environment with symbolic constant #t
  var(1, lispenv, &lispenv->env);                                 // make env a root var
  for (i = 0; primitives[i].s; ++i)                   // expand environment with primitives
	  lispenv->env = env_pair(atom(primitives[i].s, lispenv), box(PRIMITIVE, i), &lispenv->env, lispenv);
  using_history();
  BREAK_ON;                                     // enable CTRL-C break to throw error 2
  i = setjmp(state.jb);
  if (i) {
    unwind(state.n-2, lispenv);                          // unwind all but the first two, env and tru
    while (fin)                                 // close all open files
      fclose(in[--fin]);
    printf("\e[31;1mERR %d: %s\e[m", i, errors[i > 0 && i <= ERRORS ? i : 0]);
  }
  while (1) {
    putchar('\n');
    gc(1, lispenv);
    snprintf(ps, sizeof(ps), "%llu>", lispenv->sp-lispenv->hp/8);
    out = stdout;
    print(eval(readlisp(lispenv), &lispenv->env, lispenv), lispenv);
  }
}
*/
}
#endif
