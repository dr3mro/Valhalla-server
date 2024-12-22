#pragma once
#include <fmt/color.h>
#include <fmt/format.h>

#include <random>
#include <string>
#include <vector>
class Banner
{
   public:
    // Define an array of colors from fmt::color

    static fmt::color get_random_color()
    {
        std::random_device randomDevice;
        std::srand(randomDevice());

        static std::vector<fmt::color> colors = {fmt::color::alice_blue, fmt::color::antique_white, fmt::color::aqua, fmt::color::aquamarine, fmt::color::azure,
            fmt::color::beige, fmt::color::bisque, fmt::color::black, fmt::color::blanched_almond, fmt::color::blue, fmt::color::blue_violet, fmt::color::brown,
            fmt::color::burly_wood, fmt::color::cadet_blue, fmt::color::chartreuse, fmt::color::chocolate, fmt::color::coral, fmt::color::cornflower_blue,
            fmt::color::cornsilk, fmt::color::crimson, fmt::color::cyan, fmt::color::dark_blue, fmt::color::dark_cyan, fmt::color::dark_golden_rod,
            fmt::color::dark_gray, fmt::color::dark_green, fmt::color::dark_khaki, fmt::color::dark_magenta, fmt::color::dark_olive_green,
            fmt::color::dark_orange, fmt::color::dark_orchid, fmt::color::dark_red, fmt::color::dark_salmon, fmt::color::dark_sea_green,
            fmt::color::dark_slate_blue, fmt::color::dark_slate_gray, fmt::color::dark_turquoise, fmt::color::dark_violet, fmt::color::deep_pink,
            fmt::color::deep_sky_blue, fmt::color::dim_gray, fmt::color::dodger_blue, fmt::color::fire_brick, fmt::color::floral_white,
            fmt::color::forest_green, fmt::color::fuchsia, fmt::color::gainsboro, fmt::color::ghost_white, fmt::color::gold, fmt::color::golden_rod,
            fmt::color::gray, fmt::color::green, fmt::color::green_yellow, fmt::color::honey_dew, fmt::color::hot_pink, fmt::color::indian_red,
            fmt::color::indigo, fmt::color::ivory, fmt::color::khaki, fmt::color::lavender, fmt::color::lavender_blush, fmt::color::lawn_green,
            fmt::color::lemon_chiffon, fmt::color::light_blue, fmt::color::light_coral, fmt::color::light_cyan, fmt::color::light_golden_rod_yellow,
            fmt::color::light_gray, fmt::color::light_green, fmt::color::light_pink, fmt::color::light_salmon, fmt::color::light_sea_green,
            fmt::color::light_sky_blue, fmt::color::light_slate_gray, fmt::color::light_steel_blue, fmt::color::light_yellow, fmt::color::lime,
            fmt::color::lime_green, fmt::color::linen, fmt::color::magenta, fmt::color::maroon, fmt::color::medium_aquamarine, fmt::color::medium_blue,
            fmt::color::medium_orchid, fmt::color::medium_purple, fmt::color::medium_sea_green, fmt::color::medium_slate_blue, fmt::color::medium_spring_green,
            fmt::color::medium_turquoise, fmt::color::medium_violet_red, fmt::color::midnight_blue, fmt::color::mint_cream, fmt::color::misty_rose,
            fmt::color::moccasin, fmt::color::navajo_white, fmt::color::navy, fmt::color::old_lace, fmt::color::olive, fmt::color::olive_drab,
            fmt::color::orange, fmt::color::orange_red, fmt::color::orchid, fmt::color::pale_golden_rod, fmt::color::pale_green, fmt::color::pale_turquoise,
            fmt::color::pale_violet_red, fmt::color::papaya_whip, fmt::color::peach_puff, fmt::color::peru, fmt::color::pink, fmt::color::plum,
            fmt::color::powder_blue, fmt::color::purple, fmt::color::rebecca_purple, fmt::color::red, fmt::color::rosy_brown, fmt::color::royal_blue,
            fmt::color::saddle_brown, fmt::color::salmon, fmt::color::sandy_brown, fmt::color::sea_green, fmt::color::sea_shell, fmt::color::sienna,
            fmt::color::silver, fmt::color::sky_blue, fmt::color::slate_blue, fmt::color::slate_gray, fmt::color::snow, fmt::color::spring_green,
            fmt::color::steel_blue, fmt::color::tan, fmt::color::teal, fmt::color::thistle, fmt::color::tomato, fmt::color::turquoise, fmt::color::violet,
            fmt::color::wheat, fmt::color::white, fmt::color::white_smoke, fmt::color::yellow, fmt::color::yellow_green};

        return colors.at(arc4random() % colors.size());
    }

    template <typename T>
    static void print(const std::string& LABEL, const T& VALUE, fmt::color CLR1, fmt::color CLR2)
    {
        fmt::print(fmt::emphasis::bold | fg(CLR1), "{:12} : ", LABEL);
        fmt::print(fg(CLR2), "{} \n", fmt::format("{}", VALUE));
    }

    template <typename... Args>
    static void print_line(Args&&... args)
    {
        fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "{}\n\n",
            "______________________________________________________________________________________", std::forward<Args>(args)...);
    }

    static const std::string& get_logo()
    {
        static const std::string LOGO = R"(
                          ,--,     ,---,                 ,--,    ,--,
       ,---.            ,--.'|   ,--.' |               ,--.'|  ,--.'|
      /__./|            |  | :   |  |  :               |  | :  |  | :    TEAM 2024
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
        return LOGO;
    }
    static void print_logo(fmt::color COLOR) { fmt::print(fg(COLOR), "{}\n\n", get_logo()); }
};