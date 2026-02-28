#ifndef SDK_CORE_BASE_MACROS_H_
#define SDK_CORE_BASE_MACROS_H_

// 禁止类被隐式拷贝或赋值
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete

#endif  // SDK_CORE_BASE_MACROS_H_