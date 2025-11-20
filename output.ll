; ModuleID = 'Module'
source_filename = "Module"

@0 = private unnamed_addr constant [14 x i8] c"Hello, World!\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@2 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare i32 @printf(ptr, ...)

define void @hw() {
entry:
  %0 = call i32 (ptr, ...) @printf(ptr @0)
  %1 = call i32 (ptr, ...) @printf(ptr @1, i32 123)
  %2 = call i32 (ptr, ...) @printf(ptr @2)
  ret void
}

define i32 @main() {
entry:
  call void @hw()
  ret i32 0
}
