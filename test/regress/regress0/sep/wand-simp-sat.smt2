; COMMAND-LINE: --no-check-models
; EXPECT: sat
(set-logic ALL_SUPPORTED)
(declare-fun x () Int)
(assert (wand (pto x 1) (pto x 1)))
(check-sat)
