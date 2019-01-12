#include <array>
#include <iostream>
#include <string_view>

#include <unistd.h>
#include <sys/ioctl.h>

namespace csi {

thread_local char csi_buf_[ 100 ];

auto set_alternate_screen() -> std::string_view
{
    return "\033[?1049h";
}

auto reset_alternate_screen() -> std::string_view
{
    return "\033[?1049l";
}

auto reset_cursor( int row = 1, int col = 1 ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[%d;%dH", row, col );
    return { csi_buf_, len };
}

auto set_fg_color( int color ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[38;5;%dm", color );
    return { csi_buf_, len };
}

auto set_bg_color( int color ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[48;5;%dm", color );
    return { csi_buf_, len };
}

} // namespace csi

enum class Suit : uint8_t
{
    None,
    Hearts,
    Tiles,
    Clubs,
    Spades,
};

enum class Number : uint8_t
{
    None,
    Ace,
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
};

struct Card
{
    Suit m_suit = Suit::None;
    Number m_number = Number::None;
};

struct Cascade
{
    std::array< Card, 20 > m_cards; // Max number of initial cascade + 12 more cards + null
};

std::array< Cascade, 8 > cascades;

int main()
{
    struct winsize term_size;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &term_size);

    std::cout << csi::set_alternate_screen();

    std::cout << csi::reset_cursor();

    std::cout << csi::set_bg_color( 28 );
    for ( int i = 0; i < term_size.ws_row * term_size.ws_col; ++i )
        std::cout << " ";

    std::cout << csi::reset_cursor() << "Sleeping for a few seconds...";
    std::cout << csi::reset_cursor( 2, 1 ) << "Term width = " << term_size.ws_col;
    std::cout << csi::reset_cursor( 3, 1 ) << "Term height = " << term_size.ws_row << std::flush;

    std::cout << csi::reset_cursor( 5,1 ) << "Shuffling cards..." << std::flush;
    cascades[ 0 ].m_cards[ 0 ].m_suit = Suit::Hearts;
    cascades[ 0 ].m_cards[ 0 ].m_number = Number::King;
    cascades[ 0 ].m_cards[ 1 ].m_suit = Suit::Spades;
    cascades[ 0 ].m_cards[ 1 ].m_number = Number::Eight;
    cascades[ 1 ].m_cards[ 0 ].m_suit = Suit::Hearts;
    cascades[ 1 ].m_cards[ 0 ].m_number = Number::Eight;

    std::cout << csi::reset_cursor( 6,1 ) << "Drawing cards..." << std::flush;

    const int top_row = 10;
    const int start_col = 3;
    const int cascade_with = 6;

    std::cout << csi::set_bg_color( 255 ); // white bg for cards

    for ( size_t c_idx = 0; c_idx < 8; ++c_idx ) // TODO range indexed
    {
        const Cascade &cascade = cascades[ c_idx ];

        int row = top_row;
        int col = start_col + cascade_with * c_idx;

        if ( cascade.m_cards[ 0 ].m_suit == Suit::None )
        {
            std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 25 ) << "<..>";
        }
        else
        {
            std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 28 ) << u8"▀▀▀▀";
            ++row;
            std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 248 ) << u8"TODO";
            ++row;

            for ( size_t card_idx = 1; ; ++card_idx ) // TODO range indexed
            {
                const Card &card = cascade.m_cards[ card_idx ];
                if ( card.m_suit == Suit::None )
                {
                    std::cout << csi::set_fg_color( 28 )
                              << csi::reset_cursor( row,     col ) << u8"    "
                              << csi::reset_cursor( row + 1, col ) << u8"▄▄▄▄";
                    break;
                }
                else
                {
                    std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 248 ) << u8"────";
                    ++row;
                    std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 248 ) << u8"TODO";
                    ++row;
                }
            }
        }
    }
    std::cout << std::flush;

    sleep( 10 );
    std::cout << csi::reset_alternate_screen();

    return 0;
}
