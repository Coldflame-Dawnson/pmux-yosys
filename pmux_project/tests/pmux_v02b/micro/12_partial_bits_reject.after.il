module \top
  wire width 2 input 1 \sel
  wire width 2 input 2 \a
  wire width 2 input 3 \d0
  wire width 2 input 4 \d1
  wire width 2 output 5 \out
  cell $pmux \target
    parameter \WIDTH 2
    parameter \S_WIDTH 2
    connect \A \a
    connect \B { \d1 { \d0 [1] \a [0] } }
    connect \S { \sel [1] \sel [0] }
    connect \Y \out
  end
end
