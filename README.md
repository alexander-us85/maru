## Originally taken from https://github.com/attila-lendvai/maru
## What

This is a small language called Maru, written by Ian Piumarta (http://piumarta.com/software/maru/).

This repository is a clone of his work (the hg repo http://piumarta.com/hg/maru/ cloned at 2013-11-12)
and an attempt to make it more approachable for the uninitiated.

Maru is a proof of concept of a rather small, yet fully bootstrapped language.

## Status

Attila Lendvai have isolated the C bootstrap code, clarified how it is built, and wrote another
bootstrap interpreter in Common Lisp that is about 90% functional... but he ran out
of steam when he got stuck at a bug, and stopped working on this project.

Chances are low that I will jump on it again, because the code is more of an
experiment than something crystallized, and I couldn't get any feedback from
anyone when I was lost. Decyphering it all is too much effort for an unclear
amount of gain, so I put this on hold.

The branches:
 - ```official``` holds Ian's work
 - ```master``` holds a general cleanup of official
 - ```lisp-bootstrap``` holds my work on a bootstrap interpreter based off of Common Lisp
 
## How
 
There's a bootstrap ```eval``` written in C (and now in Common Lisp) for a Lisp like
language (Maru). Let's call the executable it produces ```stage 1```. Then there's
a compiler written in Maru that can emit x86 asm code. To produce ```stage 2``` this
compiler is interpreted using ```stage 1``` to compile a Maru eval written in Maru.
The executable gained from the generated and compiled asm code is ```stage 2```.
Afterwards this executable is used to again generate the asm output (```stage 3```)
and make sure that it's the same as when it was generated with the ```stage 1```
(bootstrap) eval.
