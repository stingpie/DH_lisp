(print "The world's most basic command line.\nCommands:\n  run  <filename>\n  REPL\n")
(define wut? (lambda (x) 
  (print "what??" x)
))
(wut? "huh?")
(gosub ( read "dollhouse_sandbox/useful.lisp"))
(print (env))
(while 1
	(define userinput (input))
	(print (env))
	(cond ((equal? (sublist user_input 0 4) "run ") 
	       (evoke (read (sublist user_input 4 (length user_input) )) "lisp"))
	      ((equal? (sublist user_input 0 4) "REPL")
	       (begin 
		 (define run_REPL 1) ; REPL is running
		 (print "REPL has now begun. (setq run_REPL ()) to exit.")
		 (while run_REPL
		   (eval input)
		 )
	       )
	      )
	)
)
