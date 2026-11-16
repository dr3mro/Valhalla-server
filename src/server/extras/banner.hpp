/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file banner.hpp
 * @brief Header-only helpers for printing the startup banner, configuration and
 * ASCII logo.
 *
 * @details This header defines the Banner class, a small set of static
 * functions that print the startup information to the terminal. The functions
 * print a labelled value, a decorative separator line, the project's ASCII-art
 *          logo in a random colour, and the logo plus a final flush. Everything
 * lives in the header, so no separate .cpp file or linking step is needed to
 * use it.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This file is the server's "face" at startup. Server::print_banner() calls
 * these helpers to show the effective configuration, version, port, thread
 * count and API documentation address before Drogon begins handling requests.
 * The banner is useful for confirming that environment variables were read
 * correctly and for spotting a wrong port or database setting immediately.
 * Without it the server would still run, but operators would have no quick
 * on-screen summary of which settings took effect.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Pseudo-random number generation (random_device, mt19937, distributions):
 * https://en.cppreference.com/w/cpp/numeric/random
 * - Raw string literals (R"(...)"):
 * https://en.cppreference.com/w/cpp/language/string_literal
 * - Variadic templates and parameter packs (Args&&...):
 * https://en.cppreference.com/w/cpp/language/parameter_pack
 * - The fmt formatting library: https://fmt.dev/latest/index.html
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per source file.
// for fmt::color, fg() and emphasis used to colour the output
#include <fmt/color.h>
#include <fmt/format.h>  // for fmt::print and fmt::format that render the text

// C standard library utilities kept available to the helpers
#include <cstdlib>
// for std::random_device, std::mt19937 and std::uniform_int_distribution
#include <random>
#include <string>  // for std::string, the type of the cached logo
// for the std::vector<fmt::color> palette used to pick a colour
#include <vector>

/**
 * @class Banner
 * @brief Stateless collection of static helpers for stdout startup output.
 *
 * @details All functions are static and use fmt for coloured output. There are
 * no members and no instances are required; the class only groups related
 * printing utilities. Shared static data is confined to the local statics in
 * get_random_color() and the cached logo string.
 */
class Banner  // A namespace-like class that groups the banner printing
              // functions; no object is ever created.
{
 public:  // All helpers are public so the Server class can call them.
  // Define an array of colors from fmt::color

  /**
   * @brief Picks a random colour for the ASCII logo.
   *
   * @details The palette is initialised once as a function-local static. A
   * hardware random_device seeds a fresh mt19937 on each call, and a static
   * uniform distribution selects an index in [0, colors.size() - 1].
   * @return One of the palette colours.
   * @note The returned colour is decorative; no significance should be
   * attached to it.
   * @note The shared static distribution is not locked, but this function is
   * called only once during startup, so there is no concurrent invocation in
   * this server.
   */
  static fmt::color get_random_color()  // Choose one random colour from the
                                        // palette for printing the logo.
  {
    // The palette is built once on first call and then reused on every
    // call.
    static std::vector<fmt::color> colors = {fmt::color::alice_blue,
        fmt::color::antique_white, fmt::color::aqua, fmt::color::aquamarine,
        fmt::color::azure, fmt::color::beige, fmt::color::bisque,
        fmt::color::black, fmt::color::blanched_almond, fmt::color::blue,
        fmt::color::blue_violet, fmt::color::brown, fmt::color::burly_wood,
        fmt::color::cadet_blue, fmt::color::chartreuse, fmt::color::chocolate,
        fmt::color::coral, fmt::color::cornflower_blue, fmt::color::cornsilk,
        fmt::color::crimson, fmt::color::cyan, fmt::color::dark_blue,
        fmt::color::dark_cyan, fmt::color::dark_golden_rod,
        fmt::color::dark_gray, fmt::color::dark_green, fmt::color::dark_khaki,
        fmt::color::dark_magenta, fmt::color::dark_olive_green,
        fmt::color::dark_orange, fmt::color::dark_orchid, fmt::color::dark_red,
        fmt::color::dark_salmon, fmt::color::dark_sea_green,
        fmt::color::dark_slate_blue, fmt::color::dark_slate_gray,
        fmt::color::dark_turquoise, fmt::color::dark_violet,
        fmt::color::deep_pink, fmt::color::deep_sky_blue, fmt::color::dim_gray,
        fmt::color::dodger_blue, fmt::color::fire_brick,
        fmt::color::floral_white, fmt::color::forest_green, fmt::color::fuchsia,
        fmt::color::gainsboro, fmt::color::ghost_white, fmt::color::gold,
        fmt::color::golden_rod, fmt::color::gray, fmt::color::green,
        fmt::color::green_yellow, fmt::color::honey_dew, fmt::color::hot_pink,
        fmt::color::indian_red, fmt::color::indigo, fmt::color::ivory,
        fmt::color::khaki, fmt::color::lavender, fmt::color::lavender_blush,
        fmt::color::lawn_green, fmt::color::lemon_chiffon,
        fmt::color::light_blue, fmt::color::light_coral, fmt::color::light_cyan,
        fmt::color::light_golden_rod_yellow, fmt::color::light_gray,
        fmt::color::light_green, fmt::color::light_pink,
        fmt::color::light_salmon, fmt::color::light_sea_green,
        fmt::color::light_sky_blue, fmt::color::light_slate_gray,
        fmt::color::light_steel_blue, fmt::color::light_yellow,
        fmt::color::lime, fmt::color::lime_green, fmt::color::linen,
        fmt::color::magenta, fmt::color::maroon, fmt::color::medium_aquamarine,
        fmt::color::medium_blue, fmt::color::medium_orchid,
        fmt::color::medium_purple, fmt::color::medium_sea_green,
        fmt::color::medium_slate_blue, fmt::color::medium_spring_green,
        fmt::color::medium_turquoise, fmt::color::medium_violet_red,
        fmt::color::midnight_blue, fmt::color::mint_cream,
        fmt::color::misty_rose, fmt::color::moccasin, fmt::color::navajo_white,
        fmt::color::navy, fmt::color::old_lace, fmt::color::olive,
        fmt::color::olive_drab, fmt::color::orange, fmt::color::orange_red,
        fmt::color::orchid, fmt::color::pale_golden_rod, fmt::color::pale_green,
        fmt::color::pale_turquoise, fmt::color::pale_violet_red,
        fmt::color::papaya_whip, fmt::color::peach_puff, fmt::color::peru,
        fmt::color::pink, fmt::color::plum, fmt::color::powder_blue,
        fmt::color::purple, fmt::color::rebecca_purple, fmt::color::red,
        fmt::color::rosy_brown, fmt::color::royal_blue,
        fmt::color::saddle_brown, fmt::color::salmon, fmt::color::sandy_brown,
        fmt::color::sea_green, fmt::color::sea_shell, fmt::color::sienna,
        fmt::color::silver, fmt::color::sky_blue, fmt::color::slate_blue,
        fmt::color::slate_gray, fmt::color::snow, fmt::color::spring_green,
        fmt::color::steel_blue, fmt::color::tan, fmt::color::teal,
        fmt::color::thistle, fmt::color::tomato, fmt::color::turquoise,
        fmt::color::violet, fmt::color::wheat, fmt::color::white,
        fmt::color::white_smoke, fmt::color::yellow,
        fmt::color::yellow_green};  // End of the colour palette.

    // 1. Obtain a random seed from the hardware
    std::random_device random_device;  // A hardware entropy source that gives a
                                       // different seed on each run.

    // 2. Seed the Mersenne Twister engine
    std::mt19937 generator(random_device());  // A pseudorandom generator seeded
                                              // from the hardware device.

    // 3. Define the index on the closed interval [0, colors.size() - 1]
    // (inclusive)
    static std::uniform_int_distribution<std::size_t> index(
        0, colors.size() -
               1);  // Evenly maps randoms onto palette indexes; built once.

    return colors.at(
        index(generator));  // Pick a random index and fetch that colour;
                            // at() checks the index is in range.
  }

  /**
   * @brief Prints one label/value line with the label and value coloured
   * independently.
   *
   * @tparam T Type of VALUE; must be formattable by fmt.
   * @param[in] LABEL Left-hand label, padded to a fixed width.
   * @param[in] VALUE Value rendered with fmt::format("{}", VALUE).
   * @param[in] CLR1 Colour of the label.
   * @param[in] CLR2 Colour of the value.
   */
  template <typename T>  // T is the type of the value; it only needs to be
                         // printable by fmt.
  static void print(const std::string& LABEL, const T& VALUE, fmt::color CLR1,
      fmt::color CLR2)  // Print a label/value pair
                        // with a colour for each part.
  {
    fmt::print(fmt::emphasis::bold | fg(CLR1), "{:12} : ",
        LABEL);  // Print the bold, coloured label padded to a fixed width.
    fmt::print(fg(CLR2), "{} \n",
        fmt::format("{}",
            VALUE));  // Print the value in its own colour, then move to the
                      // next line.
  }

  /**
   * @brief Prints a bold red separator line used to delimit banner sections.
   *
   * @tparam Args Extra arguments forwarded to fmt::print; call sites normally
   * pass none.
   * @param[in] args Arguments forwarded to fmt::print after the separator
   * format string.
   */
  template <typename... Args>  // A parameter pack lets callers optionally
                               // supply extra values for fmt.
  static void print_line(Args&&... args)  // Print a bold red
                                          // separator between
                                          // banner sections.
  {
    fmt::print(fmt::emphasis::bold | fg(fmt::color::red),
        "{}\n\n",  // Print in bold red, followed by two newlines for
                   // spacing.
        "__________________________________________________________________"
        "____________________",
        std::forward<Args>(args)...);  // Separator line.
  }

  /**
   * @brief Returns the project's ASCII-art logo.
   *
   * @return A reference to a function-local static string; do not mutate it.
   */
  static const std::string&
  get_logo()  // Return a reference to the cached ASCII-art logo string.
  {
    // A raw string literal keeps the artwork exactly as typed, with no
    // escaping needed.
    static const std::string LOGO = R"(
                          ,--,     ,---,                 ,--,    ,--,
       ,---.            ,--.'|   ,--.' |               ,--.'|  ,--.'|
      /__./|            |  | :   |  |  :               |  | :  |  | :    TEAM 2027
 ,---.;  ; |            :  : '   :  :  :               :  : '  :  : '
/___/ \  | |   ,--.--.  |  ' |   :  |  |,--.  ,--.--.  |  ' |  |  ' |     ,--.--.
\   ;  \ ' |  /       \ '  | |   |  :  '   | /       \ '  | |  '  | |    /       \
 \   \  \: | .--.  .-. ||  | :   |  |   /' :.--.  .-. ||  | :  |  | :   .--.  .-. |
  ;   \  ' .  \__\/: . .'  : |__ '  :  | | | \__\/: . .'  : |__'  : |__  \__\/: . .
   \   \   '  ," .--.; ||  | '.'||  |  ' | : ," .--.; ||  | '.'|  | '.'| ," .--.; |
    \   `  ; /  /  ,.  |;  :    ;|  :  :_:,'/  /  ,.  |;  :    ;  :    ;/  /  ,.  |
     :   \ |;  :   .'   \  ,   / |  | ,'   ;  :   .'   \  ,   /|  ,   /;  :   .'   \
      '---" |  ,     .-./---`-'  `--''     |  ,     .-./---`-'  ---`-' |  ,     .-./
             `--`---'                       `--`---'                    `--`---'
)";
    return LOGO;  // Hand back the cached logo; it is const so callers
                  // cannot accidentally change it.
  }
  /**
   * @brief Prints the ASCII-art logo in the given colour.
   * @param[in] COLOR Colour used for the logo text.
   * @see get_logo()
   */
  static void print_logo(fmt::color COLOR)
  {
    fmt::print(fg(COLOR), "{}\n\n", get_logo());
  }  // Print logo in that colour, then a blank line.
};
