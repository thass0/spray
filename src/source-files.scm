(load  "src/colorize.scm")

(module source-files (print-source)
  (import scheme
	  (chicken base)
	  (only (chicken string) string-split conc)
	  (only (chicken io) read-string)
	  (only (chicken file) file-exists?)
	  (only (srfi-1) fold)
	  (srfi-69))			; hash map

  (import colorizer)

  (define source-files
    (make-hash-table equal?))

  ;;; Return a vector of all lines in the file behind
  ;;; filepath or #f if the file can't be read.
  (define (file->vector filepath)
    (call-with-input-file filepath
      (lambda (port)
	(if (not port)
	    #f
	    (let ((content (read-string #f port)))
	      (if (not content)
		  #f
		  (list->vector (string-split content "\n" #t))))))))
  
  ;;; Load all lines in the file behind `filepath` into
  ;;; the `source-files` hash table. Returns #f if the
  ;;; file doesn't exists.
  (define (load-source-lines! filepath)
    (let ((lines (file->vector filepath)))
      (if (not lines)
	  #f
	  (begin
	    (hash-table-set! source-files
			     filepath
			     lines)
	    ;; Return what we've just inserted.
	    (hash-table-ref source-files
			    filepath)))))

  ;;; Return the lines in the file behind `filepath`
  ;;; or #f if `filepath` doesn't exits.
  (define (get-source-lines filepath)
    (let ((lookup (hash-table-ref/default source-files
					  filepath
					  #f)))
      (if (not lookup)
	  (load-source-lines! filepath)
	  lookup)))

  ;;; Concatenate the strings in vec at
  ;;; positions start to end (inclusive).
  ;;; Stops at the end of the vector if `end`
  ;;; is larger than the vector's length.
  ;;; This is only used until `colorize` has been
  ;;; changes to accepet a list of lines.
  (define (conc-string-subvector vec start end)
    (let ((end (if (> end (vector-length vec))
		   (vector-length vec)
		   end)))
      (foldr conc
	     ""
	     (map
	      (lambda (line)
		(conc line "\n"))
	      (vector->list
	       (subvector vec start end))))))

  (define (start-lineno lineno n-context-lines)
    (if (> lineno n-context-lines)
	(- lineno n-context-lines)
        1))

  (define (end-lineno lineno n-context-lines)
    (+ lineno n-context-lines 1
       ;; Extend window downward if there
       ;; aren't enough lines above.
       (if (< lineno n-context-lines)
	   (- n-context-lines lineno)
	   0)))

  (define (line-window lines lineno n-context-lines)
    (let ((start-lineno (start-lineno lineno n-context-lines))
	  (end-lineno (end-lineno lineno n-context-lines)))
      ;; Line numbers are one-indexed. Vectors start at 0.
      (conc-string-subvector lines
			     (- start-lineno 1)
			     (- end-lineno 1))))

  (define (print-source filepath lineno n-context-lines use-color)
    (let ((lines (get-source-lines filepath)))
      (if (not lines)
	  -1
	  (begin
    	    (print-colored
	     (colorize
	      (line-window lines lineno n-context-lines))
	     (start-lineno lineno n-context-lines)
	     lineno
	     use-color)
	    0))))
  )  ; module source-files.

(import (chicken foreign)
	(chicken platform)
	source-files)

(define-external (print_source_extern (c-string filepath)
				      (unsigned-int lineno)
				      (unsigned-int n_context_lines)
				      (bool use-color))
  int
  (print-source filepath lineno n_context_lines use-color))

(return-to-host)
