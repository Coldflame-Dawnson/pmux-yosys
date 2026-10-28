autoidx 1
module \top
  wire width 12 \f1
  wire width 12 \g1
  wire width 7 \sel
  wire width 3 input 1 \state
  wire width 12 \x1
  wire width 12 \x2
  wire width 12 \x3
  wire width 12 \y0
  wire width 12 \y1
  wire width 12 \y2
  wire width 12 \y3
  wire width 12 input 2 \a0
  wire width 12 input 3 \a1
  wire width 12 output 4 \out0
  wire width 12 output 5 \out1
  cell $eq \eq0
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'001
    connect \Y \sel [0]
  end
  cell $eq \eq1
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'010
    connect \Y \sel [1]
  end
  cell $eq \eq2
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'011
    connect \Y \sel [2]
  end
  cell $eq \eq3
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'100
    connect \Y \sel [3]
  end
  cell $eq \eq4
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'101
    connect \Y \sel [4]
  end
  cell $eq \eq5
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'110
    connect \Y \sel [5]
  end
  cell $eq \eq6
    parameter \A_SIGNED 0
    parameter \B_SIGNED 0
    parameter \A_WIDTH 3
    parameter \B_WIDTH 3
    parameter \Y_WIDTH 1
    connect \A \state
    connect \B 3'111
    connect \Y \sel [6]
  end
  cell $pmux \p0
    parameter \WIDTH 12
    parameter \S_WIDTH 7
    connect \A \a0
    connect \B { \y3 \x3 \y2 \x2 \y1 \x1 \f1 }
    connect \S \sel
    connect \Y \y0
  end
  cell $pmux \p1
    parameter \WIDTH 12
    parameter \S_WIDTH 7
    connect \A \a1
    connect \B { \x3 \y3 \x2 \y2 \x1 \y1 \g1 }
    connect \S \sel
    connect \Y \y1
  end
  connect \out0 \y0
  connect \out1 \y1
end
