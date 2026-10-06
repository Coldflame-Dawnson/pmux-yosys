module \top
  wire width 2 input 1 \sel
  wire width 1 input 2 \a
  wire width 1 input 3 \d0
  wire width 1 input 4 \d1
  wire width 1 output 5 \out
  cell $pmux \target
    parameter \WIDTH 1
    parameter \S_WIDTH 2
    connect \A \a
    connect \B { \a \d0 }
    connect \S { \sel [1] \sel [0] }
    connect \Y \out
  end
end
