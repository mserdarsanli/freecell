#include <algorithm>
#include <array>
#include <cctype>
#include <csignal>
#include <functional>
#include <iostream>
#include <random>
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
    case Suit::Hearts:    return u8"♥";
    case Suit::Diamonds:  return u8"♦️";
    case Suit::Clubs:     return u8"♣";
    case Suit::Spades:    return u8"♠️";
    default:              return "?";
    }
}

int get_color( const Suit &s )
{
    switch ( s )
    {
    case Suit::Hearts:    return 196;
    case Suit::Diamonds:  return 196;
    case Suit::Clubs:     return 232;
    case Suit::Spades:    return 232;
    default:              return 0;
    }
}

enum class Number : uint8_t
{
    None,
    Ace, Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Jack, Queen, King,
};

std::string_view to_str( const Number &n )
{
    static const char* strs[] = {
        " ?",
        " A", " 2", " 3", " 4", " 5", " 6", " 7", " 8", " 9", "10", " J", " Q", " K",
    };
    return strs[ static_cast< int >( n ) ];
}

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

struct winsize term_size;
int cursor_row = 1;
int cursor_col = 0;

int selected_row = -1;
int selected_col = -1;

bool quit_confirmation = false;
bool running = true;

uint64_t game_seed;

// TODO calculation for movable card count is not done yet
// TODO add shortcut to send all available to foundations
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

            if ( selected_row == 1 && selected_col == cursor_col )
            {
                // Deselect if selected element is sent to foundation
                selected_row = -1;
                selected_col = -1;
            }
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

    for ( int c_idx = 0; c_idx < 8; ++c_idx )
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
            for ( int card_idx = 0; card_idx < cascade.size ; ++card_idx )
            {
                const Card &card = cascade.m_cards[ card_idx ];

                int attrs = 0;
                attrs |= ( card_idx < cascade.size - 1 ? CardAttr::HasCardAbove : 0 );
                attrs |= ( card_idx > 0 ? CardAttr::HasCardBelow : 0 );
                attrs |= ( card_idx == cascade.size - 1 && selected_row == 1 && selected_col == c_idx ? CardAttr::Selected : 0 );

                draw_card( card, row + 2 * card_idx, col, attrs );
            }
        }
    }

    if ( cursor_row == 1 )
    {
        int row = top_row;
        int col = start_col + cascade_width * cursor_col;

        std::cout << csi::set_bg_color( 28 )
                  << csi::set_fg_color( 202 )
                  << csi::reset_cursor( row + 2 + 2 * cascades[ cursor_col ].size, col - 1 ) << u8"└─────┘";
    }

    if ( quit_confirmation )
    {
        std::cout << csi::set_bg_color( 196 ) << csi::set_fg_color( 255 )
                  << csi::set_bright()
                  << csi::reset_cursor( top_row + 15, start_col + 23 ) << "               "
                  << csi::reset_cursor( top_row + 16, start_col + 23 ) << "  QUIT? (y/n)  "
                  << csi::reset_cursor( top_row + 17, start_col + 23 ) << "               "
                  << csi::set_no_bright();
    }

    std::cout << csi::set_bg_color( 16 ) << csi::set_fg_color( 231 )
              << csi::reset_cursor( top_row + 43, frame_start_col ) << "[arrow keys]: move cursor"
              << csi::reset_cursor( top_row + 44, frame_start_col ) << "[space]: select/deselect/move"
              << csi::reset_cursor( top_row + 45, frame_start_col ) << "[q]: quit"
              << csi::reset_cursor( top_row + 46, frame_start_col ) << "[enter]: send to foundation"
              // Right side
              << csi::reset_cursor( top_row + 43, frame_start_col + 53 ) << "Seed = " << game_seed;


    std::cout << std::flush;
}

const char usage[] = R"(
usage: freecell [--seed 7-digit-num]
)";

enum class Key
{
    Unknown,
    Q,
    Y,
    N,
    Space,
    Enter,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
};

Key extract_key( std::string_view &input )
{
    switch ( input[ 0 ] )
    {
    case 'q': case 'Q': input = input.substr( 1 ); return Key::Q;
    case 'y': case 'Y': input = input.substr( 1 ); return Key::Y;
    case 'n': case 'N': input = input.substr( 1 ); return Key::N;
    case ' ':           input = input.substr( 1 ); return Key::Space;
    case '\r':          input = input.substr( 1 ); return Key::Enter;
    case '\033':
        if ( input[ 1 ] == '[' && input[ 2 ] == 'A' ) { input = input.substr( 3 ); return Key::ArrowUp; }
        if ( input[ 1 ] == '[' && input[ 2 ] == 'B' ) { input = input.substr( 3 ); return Key::ArrowDown; }
        if ( input[ 1 ] == '[' && input[ 2 ] == 'C' ) { input = input.substr( 3 ); return Key::ArrowRight; }
        if ( input[ 1 ] == '[' && input[ 2 ] == 'D' ) { input = input.substr( 3 ); return Key::ArrowLeft; }
    }

    // Unknown char sequence
    std::cerr << "Unhandled data of size = " << input.size() << "\n";
    std::cerr << "Bytes:\n";
    for ( char c : input )
    {
        int val = static_cast< unsigned int >( static_cast< unsigned char >( c ) );
        std::cerr << "  - " << val;
        if ( isprint( val ) )
        {
            std::cerr << "[" << (char)val << "]";
        }
        std::cerr << "\n";
    }
    input = std::string_view();
    return Key::Unknown;
}

void process_key( Key k )
{
    if ( quit_confirmation )
    {
        switch ( k )
        {
        case Key::Y:
            running = false;
            return;
        case Key::N:
            quit_confirmation = false;
            return;
        default:
            return;
        }
    }

    switch ( k )
    {
    case Key::Q:
        quit_confirmation = true;
        return;
    case Key::Space:
        if ( selected_row == -1 )
        {
            // Select non empty cells/cascades
            if ( ( cursor_row == 0 && cells[ cursor_col ] ) || ( cursor_row == 1 && cascades[ cursor_col ].size ) )
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
        return;
    case Key::Enter:
        // Enter, move item to foundation
        try_move_to_foundation();
        return;
    case Key::ArrowUp:
        if ( cursor_row > 0 )
        {
            --cursor_row;
            if ( cursor_col > 3 )
            {
                cursor_col = 3;
            }
        }
        return;
    case Key::ArrowDown:
        if ( cursor_row < 1 )
        {
            ++cursor_row;
        }
        return;
    case Key::ArrowLeft:
        if ( cursor_col > 0 )
        {
            --cursor_col;
        }
        return;
    case Key::ArrowRight:
        if ( ( cursor_row == 0 && cursor_col < 3 ) || ( cursor_row == 1 && cursor_col < 7 ) )
        {
            ++cursor_col;
        }
        return;
    default:
        return;
    }
}

int main( int argc, char* argv[] )
{
    for ( int i = 1; i < argc; )
    {
        using namespace std::literals;
        if ( argv[ i ] == "--help"sv )
        {
            std::cerr << usage + 1;
            return 0;
        }

        if ( argv[ i ] == "--seed"sv )
        {
            if ( i + 1 >= argc )
            {
                std::cerr << "--seed requires a value\n";
                return 1;
            }

            std::string_view n = argv[ i + 1 ];
            if ( n.size() != 7 || ! std::all_of( n.begin(), n.end(), ::isdigit ) || n[ 0 ] == '0' )
            {
                std::cerr << "Invalid value: " << n << "\n";
                return 1;
            }

            game_seed = std::stoull( argv[ i + 1 ] );

            i += 2;
            continue;
        }

        std::cerr << "Unknown argument: " << argv[ i ] << "\n";
        return 1;
    }

    if ( game_seed == 0 )
    {
        std::random_device rd;
        do {
            game_seed = rd();
        } while ( game_seed < 1000000 || game_seed > 9999999 );
    }

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

        std::shuffle( deck.begin(), deck.end(), std::mt19937_64( game_seed ) );

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

    signal( SIGWINCH, []( int )
    {
        ioctl(STDIN_FILENO, TIOCGWINSZ, &term_size);
        draw_frame();
    });

    while ( running )
    {
        draw_frame();

        char input_buf[ 100 ];
        int s = read( STDIN_FILENO, input_buf, 100 );

        std::string_view input( input_buf, s );

        while ( input.size() )
        {
            std::cerr << "Processing input of size = " << input.size() << "\n";
            process_key( extract_key( input ) );
        }
    }


    std::cerr << "Bye!\n";
    std::cout << csi::show_cursor() << csi::reset_alternate_screen();
    tcsetattr( STDIN_FILENO, TCSANOW, &old_attr );
    std::cout << "Bye!\n";

    return 0;
}
