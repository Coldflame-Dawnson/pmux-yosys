module \top
  wire width 3 input 1 \ctrl
  wire width 1 \s0
  wire width 1 input 2 \a
  wire width 1 input 3 \d0
  wire width 1 output 4 \out
  cell $eq \cmp0
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \ctrl
    connect \B 3'000
    connect \Y \s0
  end
  cell $pmux \target
    parameter \WIDTH 1
    parameter \S_WIDTH 1
    connect \A \a
    connect \B \a
    connect \S \s0
    connect \Y \out
  end
end
