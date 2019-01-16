#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <string_view>

#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

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

auto hide_cursor() -> std::string_view
{
    return "\033[?25l";
}

auto show_cursor() -> std::string_view
{
    return "\033[?25h";
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

auto set_bright() -> std::string_view
{
    return "\033[1m";
}

auto set_no_bright() -> std::string_view
{
    return "\033[22m";
}

} // namespace csi

bool starts_with( std::string_view a, std::string_view b )
{
    return a.size() >= b.size() && a.substr( 0, b.size() ) == b;
}

enum class Suit : uint8_t
{
    None,
    Hearts,
    Diamonds,
    Clubs,
    Spades,
};

std::string_view to_str( const Suit &s )
{
    switch ( s )
    {
    case Suit::None:      return "?";
    case Suit::Hearts:    return u8"♥";
    case Suit::Diamonds:  return u8"♦️";
    case Suit::Clubs:     return u8"♣";
    case Suit::Spades:    return u8"♠️";
    }
};

int get_color( const Suit &s )
{
    switch ( s )
    {
    case Suit::None:      return 0;
    case Suit::Hearts:    return 196;
    case Suit::Diamonds:  return 196;
    case Suit::Clubs:     return 232;
    case Suit::Spades:    return 232;
    }
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

std::string_view to_str( const Number &n )
{
    switch ( n )
    {
    case Number::None:   return " ?";
    case Number::Ace:    return " A";
    case Number::Two:    return " 2";
    case Number::Three:  return " 3";
    case Number::Four:   return " 4";
    case Number::Five:   return " 5";
    case Number::Six:    return " 6";
    case Number::Seven:  return " 7";
    case Number::Eight:  return " 8";
    case Number::Nine:   return " 9";
    case Number::Ten:    return "10";
    case Number::Jack:   return " J";
    case Number::Queen:  return " Q";
    case Number::King:   return " K";
    }
};

struct Card
{
    Suit m_suit = Suit::None;
    Number m_number = Number::None;

    operator bool() const
    {
        return m_suit != Suit::None;
    }

    int foundation_id() const
    {
        return static_cast< int >( m_suit ) - 1;
    }

    bool can_move_under( const Card &ot ) const
    {
        return get_color( this->m_suit ) != get_color( ot.m_suit )
            && static_cast< int >( this->m_number ) + 1 == static_cast< int >( ot.m_number );
    }
};

struct Cascade
{
    std::array< Card, 20 > m_cards; // Max number of initial cascade + 12 more cards + null
    int size = 0;
};

std::array< Cascade, 8 > cascades;
std::array< Card, 4 > cells;
std::array< Card, 4 > foundations;

std::ostream& operator<<( std::ostream &out, const Card &c )
{
    out << csi::set_bright()
        << csi::set_fg_color( get_color( c.m_suit ) ) << " " << to_str( c.m_number ) << to_str( c.m_suit ) << " "
        << csi::set_no_bright();

    return out;
}

struct winsize term_size; // TODO react to SIGWINCH
int cursor_row = 1;
int cursor_col = 0;

int selected_row = -1;
int selected_col = -1;

bool running = true;

// TODO calculation for movable card count is not done yet
// TODO add confirmation on exit
// TODO add shortcut to send all available to foundations
// TODO display available keys
// TODO seed rng
void try_move()
{
    // Tries to move from seleected to cursor

    if ( selected_row == 1 && cursor_row == 1 )
    {
        Cascade &from_cascade = cascades[ selected_col ];
        Cascade &to_cascade = cascades[ cursor_col ];

        // TODO whem moving to empty cascade, this moves all available items, which is not always desired
        if ( to_cascade.size == 0 )
        {
            int num_cards = 1;

            while ( num_cards < from_cascade.size && from_cascade.m_cards[ from_cascade.size - num_cards ].can_move_under( from_cascade.m_cards[ from_cascade.size - num_cards - 1 ] ) )
            {
                ++num_cards;
            }

            std::copy( from_cascade.m_cards.begin() + from_cascade.size - num_cards,
                       from_cascade.m_cards.begin() + from_cascade.size,
                       to_cascade.m_cards.begin() + to_cascade.size );

            from_cascade.size -= num_cards;
            to_cascade.size += num_cards;
            selected_row = -1;
            selected_col = -1;
            return;
        }

        // move from one cascade to another
        for ( int num_cards = 1; num_cards <= from_cascade.size ; ++num_cards )
        {
            if ( num_cards > 1 )
            {
                if ( ! from_cascade.m_cards[ from_cascade.size - num_cards + 1 ].can_move_under( from_cascade.m_cards[ from_cascade.size - num_cards ] ) )
                {
                    break;
                }
            }

            if ( from_cascade.m_cards[ from_cascade.size - num_cards ].can_move_under( to_cascade.m_cards[ to_cascade.size - 1 ] ) )
            {
                std::copy( from_cascade.m_cards.begin() + from_cascade.size - num_cards,
                           from_cascade.m_cards.begin() + from_cascade.size,
                           to_cascade.m_cards.begin() + to_cascade.size );

                from_cascade.size -= num_cards;
                to_cascade.size += num_cards;
                selected_row = -1;
                selected_col = -1;
                break;
            }
        }
    }
    else if ( selected_row == 1 && cursor_row == 0 )
    {
        if ( cells[ cursor_col ] )
        {
            return;
        }

        Cascade &from_cascade = cascades[ selected_col ];
        cells[ cursor_col ] = from_cascade.m_cards[ from_cascade.size-- - 1 ];
        selected_row = -1;
        selected_col = -1;
        return;
    }
    else if ( selected_row == 0 && cursor_row == 1 )
    {
        Cascade &to_cascade = cascades[ cursor_col ];

        if ( to_cascade.size == 0 || cells[ selected_col ].can_move_under( to_cascade.m_cards[ to_cascade.size - 1 ] ) )
        {
            to_cascade.m_cards[ to_cascade.size++ ] = cells[ selected_col ];
            cells[ selected_col ].m_suit = Suit::None;
            selected_row = -1;
            selected_col = -1;
            return;
        }
    }
}

bool try_move_to_foundation( const Card &c )
{
    if ( !c )
    {
        return false;
    }

    Card &foundation_card = foundations[ c.foundation_id() ];

    if ( c.m_number == Number::Ace
      || static_cast< int >( c.m_number ) == static_cast< int >( foundation_card.m_number ) + 1 )
    {
        foundation_card = c;
        return true;
    }

    return false;
}

void try_move_to_foundation()
{
    // TODO bug if selected element is moved to foundation?

    if ( cursor_row == 0 )
    {
        if ( try_move_to_foundation( cells[ cursor_col ] ) )
        {
            cells[ cursor_col ].m_suit = Suit::None;
        }
    }
    else // Cursor row == 1
    {
        Cascade &cascade = cascades[ cursor_col ];
        if ( cascade.size == 0 )
        {
            return;
        }

        if ( try_move_to_foundation( cascade.m_cards[ cascade.size - 1 ] ) )
        {
            --cascade.size;
        }
    }
}

// Beware of above/below distinction, since cards above are rendered below in the terminal..
enum CardAttr
{
    HasCardAbove = 1,
    HasCardBelow = 2,
    Selected = 4,
    EmptySlot = 8,
};

void draw_card( const Card &c, int row, int col, int attrs = 0 )
{
    if ( attrs & CardAttr::EmptySlot )
    {
        std::cout << csi::set_bg_color( 247 ) << csi::set_fg_color( 28 )
                  << csi::reset_cursor( row,     col ) << u8"▀▀▀▀▀"
                  << csi::reset_cursor( row + 1, col ) << u8"     "
                  << csi::reset_cursor( row + 2, col ) << u8"     "
                  << csi::reset_cursor( row + 3, col ) << u8"▄▄▄▄▄";
        return;
    }


    std::cout << csi::set_bg_color( 255 );

    if ( attrs & CardAttr::Selected )
    {
        std::cout << csi::set_fg_color( 202 ) << csi::reset_cursor( row, col - 1 ) << u8"█▀▀▀▀▀█";
    }
    else if ( attrs & CardAttr::HasCardBelow )
    {
        std::cout << csi::set_fg_color( 248 ) << csi::reset_cursor( row, col ) << u8"─────";
    }
    else
    {
        std::cout << csi::set_fg_color( 28 ) << csi::reset_cursor( row, col ) << u8"▀▀▀▀▀";
    }

    if ( attrs & CardAttr::Selected )
    {
        std::cout << csi::set_fg_color( 202 ) << csi::reset_cursor( row + 1, col - 1 ) << u8"█";
    }
    std::cout << csi::reset_cursor( row + 1, col ) << c;
    if ( attrs & CardAttr::Selected )
    {
        std::cout << csi::set_fg_color( 202 ) << csi::reset_cursor( row + 1, col + 5 ) << u8"█";
    }

    if ( attrs & CardAttr::HasCardAbove )
    {
        return;
    }

    if ( attrs & CardAttr::Selected )
    {
        std::cout << csi::set_fg_color( 202 )
                  << csi::reset_cursor( row + 2, col - 1 ) << u8"█     █"
                  << csi::reset_cursor( row + 3, col - 1 ) << u8"█▄▄▄▄▄█";
    }
    else
    {
        std::cout << csi::set_fg_color( 28 )
                  << csi::reset_cursor( row + 2, col ) << u8"     "
                  << csi::reset_cursor( row + 3, col ) << u8"▄▄▄▄▄";
    }
}

void draw_frame()
{
    // Clear screen first
    std::cout << csi::set_bg_color( 232 );
    for ( int i = 0; i < term_size.ws_row * term_size.ws_col; ++i )
        std::cout << " ";

    const int cascade_width = 8;

    const int frame_height = 48;
    const int frame_width = 8 * cascade_width + 3;
    const int frame_start_row = 10;
    const int frame_start_col = ( term_size.ws_col - frame_width ) / 2;

    // Draw frame
    std::cout << csi::set_bg_color( 28 ) << csi::set_fg_color( 255 );
    for ( int row = 0; row < frame_height; ++row )
    {
        std::cout << csi::reset_cursor( frame_start_row + row, frame_start_col );

        std::cout << ( row ==  0 ? u8"┌" :
                       row == frame_height - 1 ? u8"└" : "│" );

        for ( int col = 1; col < frame_width - 1; ++col )
        {
            std::cout << ( ( row ==  0 || row == frame_height - 1 ) ? u8"─" : " " );
        }

        std::cout << ( row ==  0 ? u8"┐" :
                       row == frame_height - 1 ? u8"┘" : "│" );
    }

    {
        for ( int cell_idx = 0; cell_idx < 4; ++cell_idx )
        {
            int attrs = ( cells[ cell_idx ] ? 0 : CardAttr::EmptySlot ) | ( selected_row == 0 && selected_col == cell_idx ? CardAttr::Selected : 0 );
            draw_card( cells[ cell_idx ], frame_start_row + 1, frame_start_col +  2 + 7 * cell_idx, attrs );
        }

        if ( cursor_row == 0 )
        {
            std::cout << csi::set_bg_color( 28 )
                      << csi::set_fg_color( 202 )
                      << csi::reset_cursor( frame_start_row + 5, frame_start_col + 1 + 7 * cursor_col ) << u8"└─────┘";
        }

        for ( int cell_idx = 0; cell_idx < 4; ++cell_idx )
        {
            int attrs = ( foundations[ cell_idx ] ? 0 : CardAttr::EmptySlot );
            draw_card( foundations[ cell_idx ], frame_start_row + 1, frame_start_col + frame_width - 7 - cell_idx * 7, attrs );
        }
    }


    const int top_row = frame_start_row + 6;
    const int start_col = frame_start_col + 3;

    for ( size_t c_idx = 0; c_idx < 8; ++c_idx ) // TODO range indexed
    {
        std::cout << csi::set_bg_color( 255 ); // white bg for cards

        const Cascade &cascade = cascades[ c_idx ];

        int row = top_row;
        int col = start_col + cascade_width * c_idx;

        if ( cascade.m_cards[ 0 ].m_suit == Suit::None )
        {
            std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 25 ) << "<...>";
        }
        else
        {
            for ( size_t card_idx = 0; card_idx < cascade.size ; ++card_idx ) // TODO range indexed
            {
                const Card &card = cascade.m_cards[ card_idx ];

                int attrs = 0;
                attrs |= ( card_idx < cascade.size - 1 ? CardAttr::HasCardAbove : 0 );
                attrs |= ( card_idx > 0 ? CardAttr::HasCardBelow : 0 );
                attrs |= ( card_idx == cascade.size - 1 && selected_row == 1 && selected_col == c_idx ? CardAttr::Selected : 0 );

                draw_card( cascade.m_cards[ card_idx ], row + 2 * card_idx, col, attrs );
            }
        }
    }

    if ( cursor_row == 1 )
    {
        int row = top_row;
        int col = start_col + cascade_width * cursor_col;

        // TODO recalculate row
        std::cout << csi::set_bg_color( 28 )
                  << csi::set_fg_color( 202 )
                  << csi::reset_cursor( row + 2 + 2 * cascades[ cursor_col ].size, col - 1 ) << u8"└─────┘";
    }

    std::cout << std::flush;
}

int main()
{
    ioctl(STDIN_FILENO, TIOCGWINSZ, &term_size);

    // Keep around for cleanup
    struct termios old_attr;
    tcgetattr( STDIN_FILENO, &old_attr );

    {
        struct termios new_attr = old_attr;
        cfmakeraw( &new_attr );
        new_attr.c_cc[ VMIN ] = 1; // Return after 1 char
        new_attr.c_cc[ VTIME ] = 0; // Don't wait
        tcsetattr( STDIN_FILENO, TCSANOW, &new_attr );
    }

    std::cout << csi::set_alternate_screen() << csi::hide_cursor();

    std::cerr << "Term width = " << term_size.ws_col << "\n";
    std::cerr << "Term height = " << term_size.ws_row << "\n";

    {
        std::array< Card, 52 > deck;
        for ( uint8_t suit = 1; suit <= 4; ++suit )
        {
            for ( uint8_t number = 1; number <= 13; ++number )
            {
                Card &card = deck[ ( suit - 1 ) * 13 + ( number - 1 ) ];
                card.m_suit = static_cast< Suit >( suit );
                card.m_number = static_cast< Number >( number );
            }
        }

        std::random_shuffle( deck.begin(), deck.end() );

        Cascade *cur_cascade = &cascades[ 0 ];
        for ( const Card &c : deck )
        {
            cur_cascade->m_cards[ cur_cascade->size++ ] = c;
            ++cur_cascade;
            if ( cur_cascade == cascades.end() )
            {
               cur_cascade = &cascades[ 0 ];
            }
        }
    }

    // This could be a static/constexpr map
    std::map< std::string_view, std::function< void() > > actions;
    actions[ "q" ] = []()
    {
        running = false;
    };

    actions[ " " ] = []()
    {
        if ( selected_row == -1 )
        {
            // Select non empty cells/cascades
            if ( cursor_row == 0 && cells[ cursor_col ] || cursor_row == 1 && cascades[ cursor_col ].size )
            {
                selected_row = cursor_row;
                selected_col = cursor_col;
            }
        }
        else if ( selected_row == cursor_row && selected_col == cursor_col )
        {
            // Deselect
            selected_row = -1;
            selected_col = -1;
        }
        else
        {
            try_move();
        }
    };

    actions[ "\r" ] = []()
    {
        // Enter, move item to foundation
        try_move_to_foundation();
    };

    actions[ "\033[A" ] = []()
    {
        // Up
        if ( cursor_row > 0 )
        {
            --cursor_row;
            if ( cursor_col > 3 )
            {
                cursor_col = 3;
            }
        }
    };
    actions[ "\033[B" ] = []()
    {
        // Down
        if ( cursor_row < 1 )
        {
            ++cursor_row;
        }
    };
    actions[ "\033[D" ] = []()
    {
        // Left
        if ( cursor_col > 0 )
        {
            --cursor_col;
        }
    };
    actions[ "\033[C" ] = []()
    {
        // Right
        if ( cursor_row == 0 && cursor_col < 3 || cursor_row == 1 && cursor_col < 7 )
        {
            ++cursor_col;
        }
    };

    while ( running )
    {
        draw_frame();

        char input_buf[ 100 ];
        int s = read( STDIN_FILENO, input_buf, 100 );

        std::string_view input( input_buf, s );

        process_input:
        auto it = actions.lower_bound( input );

        if ( it->first == input )
        {
            it->second();
            continue;
        }

        if ( it != actions.begin() )
        {
            --it;

            if ( starts_with( input, it->first ) )
            {
                it->second();
                input = input.substr( it->first.size() );
                goto process_input;
            }
        }

        std::cerr << "Unhandled data of size = " << s << "\n";
        std::cerr << "Bytes:\n";
        for ( int i = 0; i < s; ++i )
        {
            int val = static_cast< unsigned int >( static_cast< unsigned char >( input_buf[ i ] ) );
            std::cerr << "  - " << val;
            if ( isprint( val ) )
            {
                std::cerr << "[" << (char)val << "]";
            }
            std::cerr << "\n";
        }
    }


    std::cerr << "Bye!\n";
    std::cout << csi::show_cursor() << csi::reset_alternate_screen();
    tcsetattr( STDIN_FILENO, TCSANOW, &old_attr );
    std::cout << "Bye!\n";

    return 0;
}
