; ModuleID = 'Module'
source_filename = "Module"

@0 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

declare i32 @sum([100 x i32])

define i32 @main() {
entry:
  %i9 = alloca i32, align 4
  %r = alloca i32, align 4
  %i = alloca i32, align 4
  %arr = alloca [100 x i32], align 4
  store [100 x i32] zeroinitializer, ptr %arr, align 4
  %arrayidx = getelementptr [100 x i32], ptr %arr, i32 0, i32 1
  store i32 10, ptr %arrayidx, align 4
  %arrayidx1 = getelementptr [100 x i32], ptr %arr, i32 0, i32 2
  store i32 20, ptr %arrayidx1, align 4
  %arrayidx2 = getelementptr [100 x i32], ptr %arr, i32 0, i32 3
  store i32 30, ptr %arrayidx2, align 4
  store i32 1, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.body, %entry
  %loopvar = load i32, ptr %i, align 4
  %loopcond = icmp sle i32 %loopvar, 3
  br i1 %loopcond, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %loadtmp = load i32, ptr %i, align 4
  %arrayidx3 = getelementptr [100 x i32], ptr %arr, i32 0, i32 %loadtmp
  %loadtmp4 = load i32, ptr %arrayidx3, align 4
  %0 = call i32 (ptr, ...) @printf(ptr @0, i32 %loadtmp4)
  %nextval = add i32 %loopvar, 1
  store i32 %nextval, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %loadtmp5 = load [100 x i32], ptr %arr, align 4
  %calltmp = call i32 @sum([100 x i32] %loadtmp5)
  store i32 %calltmp, ptr %r, align 4
  store i32 3, ptr %i9, align 4
  br label %for.cond6

for.cond6:                                        ; preds = %for.body7, %for.end
  %loopvar10 = load i32, ptr %i9, align 4
  %loopcond11 = icmp sge i32 %loopvar10, 1
  br i1 %loopcond11, label %for.body7, label %for.end8

for.body7:                                        ; preds = %for.cond6
  %loadtmp12 = load i32, ptr %i9, align 4
  %arrayidx13 = getelementptr [100 x i32], ptr %arr, i32 0, i32 %loadtmp12
  %loadtmp14 = load i32, ptr %arrayidx13, align 4
  %1 = call i32 (ptr, ...) @printf.2(ptr @1, i32 %loadtmp14)
  %nextval15 = sub i32 %loopvar10, 1
  store i32 %nextval15, ptr %i9, align 4
  br label %for.cond6

for.end8:                                         ; preds = %for.cond6
  ret i32 0
}

define i32 @sum.1([100 x i32] %arr) {
entry:
  %index = alloca i32, align 4
  %elem = alloca i32, align 4
  %sum = alloca i32, align 4
  %arr1 = alloca [100 x i32], align 4
  store [100 x i32] %arr, ptr %arr1, align 4
  store i32 0, ptr %sum, align 4
  store i32 0, ptr %index, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.body, %entry
  %index2 = load i32, ptr %index, align 4
  %loopcond = icmp slt i32 %index2, 100
  br i1 %loopcond, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %elementptr = getelementptr [100 x i32], ptr %arr1, i32 0, i32 %index2
  %element = load i32, ptr %elementptr, align 4
  store i32 %element, ptr %elem, align 4
  %loadtmp = load i32, ptr %sum, align 4
  %loadtmp3 = load i32, ptr %elem, align 4
  %addtmp = add i32 %loadtmp, %loadtmp3
  store i32 %addtmp, ptr %sum, align 4
  %nextindex = add i32 %index2, 1
  store i32 %nextindex, ptr %index, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i32 0
}

declare i32 @printf(ptr, ...)

declare i32 @printf.2(ptr, ...)
