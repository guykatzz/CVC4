; COMMAND-LINE: --no-check-models
; EXPECT: sat
(set-logic ALL_SUPPORTED)
(set-info :status sat)
(declare-fun x () Int)
(assert (wand (pto x 1) (emp x)))
(check-sat)
