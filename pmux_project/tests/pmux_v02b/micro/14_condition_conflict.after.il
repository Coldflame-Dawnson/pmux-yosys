module \top
  wire width 2 input 1 \sel
  wire width 1 \not_s
  wire width 1 \conflict_s
  wire width 1 input 2 \a
  wire width 1 input 3 \d0
  wire width 1 input 4 \d1
  wire width 1 output 5 \out
  cell $not \not_sel
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \sel [0]
    connect \Y \not_s
  end
  cell $and \conflict_and
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \sel [0]
    connect \B \not_s
    connect \Y \conflict_s
  end
  cell $pmux \target
    parameter \WIDTH 1
    parameter \S_WIDTH 2
    connect \A \a
    connect \B { \d1 \d0 }
    connect \S { \sel [1] \conflict_s }
    connect \Y \out
  end
end
