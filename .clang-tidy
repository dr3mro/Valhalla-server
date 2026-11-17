---
# Static analysis configuration for clang-tidy.
#
# `Checks` is one comma-separated string, not a YAML list. Rationale for the
# disabled checks is kept in YAML comments below instead of inside the string,
# where clang-tidy would try to parse the words as check names.
Checks: >-
  clang-analyzer-*,
  -clang-analyzer-optin.core.EnumCastOutOfRange,

  bugprone-*,
  -bugprone-unchecked-optional-access,
  -bugprone-implicit-widening-of-multiplication-result,
  -bugprone-easily-swappable-parameters,
  -bugprone-exception-escape,
  -bugprone-narrowing-conversions,
  -bugprone-chained-comparison,
  -bugprone-throw-keyword-missing,
  -bugprone-use-after-move,
  -bugprone-macro-repeated-side-effects,

  modernize-*,
  -modernize-avoid-c-arrays,
  -modernize-use-auto,
  -modernize-use-emplace,
  -modernize-use-nullptr,
  -modernize-use-trailing-return-type,
  -modernize-return-braced-init-list,
  -modernize-concat-nested-namespaces,
  -modernize-use-nodiscard,
  -modernize-use-default-member-init,
  -modernize-type-traits,
  -modernize-deprecated-headers,
  -modernize-pass-by-value,
  -modernize-replace-auto-ptr,

  performance-*,
  -performance-enum-size,

  portability-*,

  readability-*,
  -readability-braces-around-statements,
  -readability-container-size-empty,
  -readability-convert-member-functions-to-static,
  -readability-else-after-return,
  -readability-function-cognitive-complexity,
  -readability-function-size,
  -readability-identifier-length,
  -readability-implicit-bool-conversion,
  -readability-isolate-declaration,
  -readability-magic-numbers,
  -readability-named-parameter,
  -readability-qualified-auto,
  -readability-redundant-access-specifiers,
  -readability-simplify-boolean-expr,
  -readability-static-definition-in-anonymous-namespace,
  -readability-uppercase-literal-suffix,
  -readability-use-anyofallof,
  -readability-avoid-return-with-void-value,
  -readability-identifier-naming,

WarningsAsErrors: >-
  clang-analyzer-core.*,
  clang-analyzer-cplusplus.*,
  clang-analyzer-security.*,
  clang-analyzer-unix.*,
  performance-move-const-arg,
  performance-unnecessary-value-param,
  readability-duplicate-include,
HeaderFilterRegex: '.*\.(c|cxx|cpp)$'
FormatStyle: none

# Why some checks are disabled:
# - bugprone-unchecked-optional-access: noisy on intentional optional reads.
# - bugprone-implicit-widening-of-multiplication-result: fires on constants.
# - bugprone-easily-swappable-parameters: too noisy for this API shape.
# - bugprone-exception-escape: false positives with no-noexcept move ctors.
# - bugprone-chained-comparison: conflicts with decomposers.
# - bugprone-throw-keyword-missing / -modernize-replace-auto-ptr /
#   -readability-identifier-naming: slow checks with little value here.
# - bugprone-use-after-move: needs custom unique_ptr support.
# - bugprone-macro-repeated-side-effects: misses CATCH_MOVE / CATCH_FORWARD
#   unevaluated contexts.
# - modernize-type-traits: historically disabled for C++14; the project is
#   C++20 now, so this is a candidate to re-enable.
# - modernize-use-nullptr: goes crazy with three-way comparison operators.
# - modernize-pass-by-value: many reports, mostly not useful.
# - readability-convert-member-functions-to-static: also listed in
#   cppcheck.supp by mistake; it belongs only here.
