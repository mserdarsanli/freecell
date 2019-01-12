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

auto set_bg_color( int color ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[48;5;%dm", color );
    return { csi_buf_, len };
}

} // namespace csi

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
    sleep( 3 );

    std::cout << csi::reset_alternate_screen();

    return 0;
}
