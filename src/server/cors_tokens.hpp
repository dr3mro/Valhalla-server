/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file cors_tokens.hpp
 * @brief Drogon-free helpers for parsing and merging CORS header token lists.
 *
 * @details This header contains small functions for working with
 * comma-separated header values used by CORS. It can tell whether a character
 * is legal in an HTTP token, trim and split a header value into tokens, compare
 *          strings without regard to ASCII case, sanitize a client-supplied
 * header list, and merge a token into an existing Vary header without
 * duplicating it. The helpers deliberately avoid depending on Drogon.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * These functions are the security and correctness layer behind the CORS code
 * in cors.hpp. When a browser sends an Access-Control-Request-Headers value,
 * this file filters out dangerous characters (such as CRLF or quotes) before
 * they are echoed into a response, preventing header-injection attacks. It also
 * keeps the Vary header tidy so caches store separate responses for different
 * origins. Because the helpers have no framework dependency, they can be tested
 * on their own, which is why a dedicated test file exists for them.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++20 ranges and views (lazy, composable sequence operations):
 * https://en.cppreference.com/w/cpp/header/ranges
 * - std::string_view (a non-owning view over text):
 * https://en.cppreference.com/w/cpp/string/basic_string_view
 * - constexpr (values/functions usable at compile time):
 * https://en.cppreference.com/w/cpp/language/constexpr
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file.

#include <algorithm>  // for std::ranges::all_of and std::ranges::equal
// for std::views::split / transform / filter used by tokens()
#include <ranges>
// for std::string built by sanitizeRequestedHeaders() and mergeVary()
#include <string>
// for std::string_view, used for borrowed, non-owning text throughout
#include <string_view>

/**
 * @namespace CorsTokens
 * @brief Drogon-free helpers for parsing and merging CORS header token lists.
 *
 * @details Kept independent of the HTTP framework so token parsing can be
 * reasoned about, and tested, on its own.
 */
namespace CorsTokens  // Group the token helpers, keeping them independent of
                      // the HTTP framework.
{
  /**
   * Punctuation permitted in an HTTP token, per the token grammar used by the
   * header parsers below.
   */
  constexpr std::string_view tokenPunctuation =
      "!#$%&'*+-.^_`|~";  // Compile-time punctuation allowed in a token.

  /**
   * @brief Reports whether a character is valid inside an HTTP token.
   *
   * @details Alphabetic, digit and the RFC token punctuation characters are
   * accepted; whitespace, comma, quote, colon and control characters are not.
   * This rejects values that could inject extra headers.
   * @param[in] c Character to classify.
   * @return true when c may appear in a token.
   */
  constexpr bool isTokenChar(char c)  // Decide whether one character is legal
                                      // inside an HTTP header token.
  {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           tokenPunctuation.find(c) != std::string_view::npos;  // Valid char.
  }

  /**
   * @brief Removes optional whitespace (spaces and tabs) from both ends of a
   * header token.
   *
   * @param[in] value View to trim; not modified.
   * @return A subview of value with leading/trailing whitespace removed, or
   * an empty view if value is all whitespace.
   */
  inline std::string_view trim(
      std::string_view value)  // Return a view with leading and trailing
                               // spaces/tabs removed.
  {
    const std::size_t first = value.find_first_not_of(
        " \t");  // Find the first character that is not a space or tab.
    if (first == std::string_view::npos)  // If there is none, the entire
                                          // view is whitespace.
    {
      return {};  // Hand back an empty view.
    }
    const std::size_t last = value.find_last_not_of(
        " \t");  // Find the last non-whitespace character.
    return value
        .substr(first,
                last - first +
                    1);  // Return the subview from first to last, inclusive.
  }

  /**
   * @brief Lazily splits a comma-separated header value into trimmed,
   * non-empty tokens.
   *
   * @details The returned range is a view: it allocates nothing and borrows
   * from value, so the caller must keep the underlying string alive while
   * iterating. Empty list elements (including ones made only of whitespace)
   *          are filtered out.
   * @param[in] value Header value to split.
   * @return A lazy range of std::string_view tokens.
   */
  inline auto tokens(
      std::string_view value)  // Build a lazy range that splits, trims and
                               // filters a header value.
  {
    return value | std::views::split(',') |  // Split at commas into sub-ranges
                                             // without copying the string.
           std::views::transform(  // Convert each sub-range into a trimmed
                                   // string_view.
               [](auto range)      // This lambda handles one split piece.
               {
                 if (range.begin() ==
                     range.end())  // An empty piece (e.g. from two
                                   // adjacent commas) needs no work.
                 {
                   return std::string_view{};  // Return an empty view
                                               // so the filter below
                                               // removes it.
                 }
                 // std::ranges::distance returns a signed ptrdiff_t; the
                 // range is non-empty here, so converting it to
                 // string_view's unsigned length is safe.
                 return trim(
                     std::string_view(&*range.begin(),
                                      static_cast<std::size_t>(
                                          std::ranges::distance(
                                              range))));  // Trim this piece's
                                                          // whitespace.
               }) |
           std::views::filter(
               [](std::string_view token)
               {
                 return !token.empty();
               });  // Drop empty tokens left by extra commas or spaces.
  }

  /**
   * @brief ASCII case-insensitive string comparison.
   *
   * @details Only A-Z are folded to a-z, which is sufficient for HTTP header
   * field names; non-ASCII bytes are compared verbatim.
   * @param[in] a First string.
   * @param[in] b Second string.
   * @return true when the strings have equal length and match after ASCII
   * lowercasing.
   */
  inline bool iequals(std::string_view a,
                      std::string_view b)  // Compare two strings ignoring the
                                           // case of ASCII letters.
  {
    auto lower = [](char c)
    {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
    };  // Fold A-Z to a-z; other bytes unchanged.
    return a.size() == b.size() &&
           std::ranges::equal(a,
                              b,
                              [lower](char x, char y)
                              {
                                return lower(x) == lower(y);
                              });  // Same length and content.
  }

  /**
   * @brief Filters a client-supplied Access-Control-Request-Headers value
   * down to syntactically valid tokens.
   *
   * @details Rejects the whole value when it is empty or longer than 1024
   * characters, then keeps only comma-separated tokens made entirely of token
   * characters and rejoins them with ", ". This prevents a preflight request
   *          from echoing CRLF, quotes or arbitrary separators into
   * Access-Control-Allow-Headers.
   * @param[in] requested Raw header value from the request.
   * @return A comma-and-space separated list of safe tokens, or an empty
   * string when nothing is acceptable. An empty result tells Cors::apply() to
   * fall back to the default header list.
   */
  inline std::string sanitizeRequestedHeaders(
      std::string_view requested)  // Keep only safe, valid tokens from a
                                   // client-supplied header value.
  {
    if (requested.empty() ||
        requested.size() > 1024)  // Reject empty or very long values
                                  // outright to avoid abuse.
    {
      return {};  // Return empty so the caller falls back to its default
                  // header list.
    }

    std::string result;  // Accumulates the safe tokens, separated by ", ".
    for (std::string_view token :
         tokens(requested))  // Iterate the lazily split and trimmed tokens.
    {
      if (!std::ranges::all_of(token,
                               isTokenChar))  // Drop any token containing
                                              // invalid characters such as CRLF
                                              // or quotes.
      {
        continue;  // Skip this token and move on to the next one.
      }
      if (!result.empty())  // Add a separator only when a token has
                            // already been written.
      {
        result += ", ";  // Separate tokens with a comma and a space.
      }
      result.append(token);  // Append the validated token to the result.
    }
    return result;  // Return the sanitized list, or empty if nothing was
                    // acceptable.
  }

  /**
   * @brief Merges a token into an existing Vary header value, avoiding
   * duplicates.
   *
   * @details Tokens are compared case-insensitively. A "*" wildcard in the
   * existing value means every response is already variable, so the value is
   * returned unchanged; likewise when the token is already present. Otherwise
   * the token is appended. The original string is returned unchanged when no
   * edit is needed, which lets the caller skip the header write.
   * @param[in] existing Current Vary header value.
   * @param[in] value Token to add.
   * @return The merged Vary value.
   */
  inline std::string mergeVary(
      std::string_view existing,
      std::string_view value)  // Append a token to Vary unless it is redundant.
  {
    std::string combined;  // Rebuilds the header list with tokens separated
                           // by ", ".
    bool        hasWildcard = false;  // True if the existing value already
                                      // contains the "*" wildcard.
    bool        present =
        false;  // True if the requested token is already in the list.
    for (std::string_view token :
         tokens(existing))  // Walk each token of the existing header value.
    {
      if (token == "*")  // A wildcard means every response is already variable.
      {
        hasWildcard = true;  // Remember that no change is needed.
      }
      if (iequals(token,
                  value))  // Compare case-insensitively, as header names are
                           // case-insensitive.
      {
        present = true;  // Remember that the token is already present.
      }
      if (!combined.empty())  // Add a separator before all tokens except
                              // the first.
      {
        combined += ", ";  // Keep the list readable and syntactically correct.
      }
      combined.append(token);  // Copy this token into the rebuilt list.
    }

    if (hasWildcard || present)  // No change is useful if it is already
                                 // wildcard or already present.
    {
      return std::string(
          existing);  // Return a copy of the original value, unchanged.
    }
    if (!combined.empty())  // If existing tokens were copied, separate the
                            // new one from them.
    {
      combined += ", ";  // Insert the separator before appending.
    }
    combined.append(value);  // Add the new token at the end of the list.
    return combined;  // Return the merged value so the caller can write it.
  }
}  // namespace CorsTokens
