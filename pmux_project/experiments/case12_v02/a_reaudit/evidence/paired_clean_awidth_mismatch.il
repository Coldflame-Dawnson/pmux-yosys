autoidx 1
module \top
  wire width 3 input 1 \state
  wire input 2 \a0
  wire input 3 \a1
  wire output 4 \y0
  wire output 5 \y1
  wire \s1
  wire \s2
  wire \s3
  wire \s4
  wire \s5
  wire \s6
  wire \s7
  cell $eq \eq1
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'001
    connect \Y \s1
  end
  cell $eq \eq2
    parameter \A_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'010
    connect \Y \s2
  end
  cell $eq \eq3
    parameter \A_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'011
    connect \Y \s3
  end
  cell $eq \eq4
    parameter \A_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'100
    connect \Y \s4
  end
  cell $eq \eq5
    parameter \A_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'101
    connect \Y \s5
  end
  cell $eq \eq6
    parameter \A_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'110
    connect \Y \s6
  end
  cell $eq \eq7
    parameter \A_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_SIGNED 0
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'111
    connect \Y \s7
  end
  cell $pmux \p0
    parameter \S_WIDTH 7
    parameter \WIDTH 1
    connect \A \a0
    connect \B 7'1010100
    connect \S { \s7 \s6 \s5 \s4 \s3 \s2 \s1 }
    connect \Y \y0
  end
  cell $pmux \p1
    parameter \S_WIDTH 7
    parameter \WIDTH 1
    connect \A \a1
    connect \B 7'0101011
    connect \S { \s7 \s6 \s5 \s4 \s3 \s2 \s1 }
    connect \Y \y1
  end
end
