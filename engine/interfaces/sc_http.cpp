// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simulationcraft.hpp"

// Cross-Platform Support for HTTP-Download =================================

// ==========================================================================
// PLATFORM INDEPENDENT SECTION
// ==========================================================================

// proxy ====================================================================

http::proxy_t proxy;

cache::cache_control_t cache::cache_control_t::singleton;

namespace { // UNNAMED NAMESPACE ==========================================

const bool HTTP_CACHE_DEBUG = false;

mutex_t cache_mutex;

const unsigned int NETBUFSIZE = 1 << 15;

struct url_cache_entry_t
{
  // Not necessarily UTF-8; may contain zero bytes. Should really be vector<uint8_t>.
  std::string result;
  std::string last_modified_header;
  cache::era_t modified, validated;

  url_cache_entry_t() :
    modified( cache::INVALID_ERA ), validated( cache::INVALID_ERA )
  {}
};

typedef std::unordered_map<std::string, url_cache_entry_t> url_db_t;
url_db_t url_db;

// cache_clear ==============================================================

void cache_clear()
{
  // writer lock
  auto_lock_t lock( cache_mutex );
  url_db.clear();
}

const char* const cookies =
  "Cookie: loginChecked=1\r\n"
  "Cookie: cookieLangId=en_US\r\n"
  // Skip arenapass 2011 advertisement .. can we please have a sensible
  // API soon?
  "Cookie: int-WOW=1\r\n"
  "Cookie: int-WOW-arenapass2011=1\r\n"
  "Cookie: int-WOW-epic-savings-promo=1\r\n"
  "Cookie: int-WOW-anniversary=1\r\n"
  "Cookie: int-EuropeanInvitational2011=1\r\n"
  "Cookie: int-dec=1\r\n";

#if defined( NO_HTTP )

// ==========================================================================
// NO HTTP-DOWNLOAD SUPPORT
// ==========================================================================

// download =================================================================

bool download( url_cache_entry_t&,
                      const std::string& )
{
  return false;
}

#elif defined( SC_WINDOWS )

// ==========================================================================
// HTTP-DOWNLOAD FOR WINDOWS
// ==========================================================================
#include <windows.h>
#include <wininet.h>

// download =================================================================

bool download( url_cache_entry_t& entry,
                      const std::string& url )
{
  // Requires cache_mutex to be held.

  class InetWrapper : private noncopyable
  {
  public:
    HINTERNET handle;

    explicit InetWrapper( HINTERNET handle_ ) : handle( handle_ ) {}
    ~InetWrapper() { if ( handle ) InternetCloseHandle( handle ); }
    operator HINTERNET () const { return handle; }
  };

  static HINTERNET hINet;
  if ( !hINet )
  {
    // hINet = InternetOpen( L"simulationcraft", INTERNET_OPEN_TYPE_PROXY, "proxy-server", NULL, 0 );
    hINet = InternetOpenW( L"simulationcraft", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0 );
    if ( ! hINet )
      return false;
  }

  std::wstring headers = io::widen( cookies );

  if ( ! entry.last_modified_header.empty() )
  {
    headers += L"If-Modified-Since: ";
    headers += io::widen( entry.last_modified_header );
    headers += L"\r\n";
  }

  InetWrapper hFile( InternetOpenUrlW( hINet, io::widen( url ).c_str(), headers.data(), static_cast<DWORD>( headers.length() ),
                                       INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0 ) );
  if ( ! hFile )
    return false;

  union
  {
    char *chars;
    wchar_t *wchars;
  } buffer;
  buffer.chars = ( char * ) malloc( NETBUFSIZE );
  buffer.wchars = ( wchar_t * ) malloc( ( NETBUFSIZE + 1 ) / 2 );
  if ( buffer.chars != NULL )
  {
    buffer.chars[0] = '\0';
    DWORD amount = sizeof( buffer );
    if ( !HttpQueryInfoA( hFile, HTTP_QUERY_STATUS_CODE, buffer.chars, &amount, 0 ) )
      return false;

    if ( !std::strcmp( buffer.chars, "304" ) )
    {
      entry.validated = cache::era();
      return true;
    }

    entry.result.clear();
    while ( InternetReadFile( hFile, buffer.chars, sizeof( buffer ), &amount ) )
    {
      if ( amount == 0 )
        break;
      entry.result.append( buffer.chars, buffer.chars + amount );
    }

    entry.modified = entry.validated = cache::era();

    entry.last_modified_header.clear();
    amount = sizeof( buffer );
    DWORD index = 0;
    if ( buffer.wchars != NULL )
    {
      if ( HttpQueryInfoW( hFile, HTTP_QUERY_LAST_MODIFIED, buffer.wchars, &amount, &index ) )
        entry.last_modified_header = io::narrow( buffer.wchars );
    }

    free( buffer.chars );
  }
  return true;
}

#else

// ==========================================================================
// HTTP-DOWNLOAD FOR POSIX COMPLIANT PLATFORMS
// ==========================================================================

#include <curl/curl.h>

size_t write_callback(char *ptr, size_t size, size_t nitems, std::string *buffer)
{
    size_t len = size * nitems;
    buffer->append(ptr, len);
    return len;
}

size_t header_callback(char *ptr, size_t size, size_t nitems, std::string *lastmod)
{
    size_t len = size * nitems;
    if (lastmod->empty())
    {
        std::string header(ptr, len);
        if (header.substr(0, 15) == "Last-Modified: ")
            lastmod->assign(header.substr(15, header.length() - 15 - 2));
    }
    return len;
}

bool download(url_cache_entry_t& entry, const std::string& url)
{
    CURL *request = curl_easy_init();
    if (request == NULL)
    {
        std::cerr << "curl: error creating easy handle" << std::endl;
        return false;
    }

    curl_slist *headers = NULL;
    std::string hstr(cookies);
    for (size_t last = 0, next = 0; (next = hstr.find("\r\n", last)) != std::string::npos; last = next + 2)
    {
        std::string line = hstr.substr(last, next - last);
        if (!line.empty())
            headers = curl_slist_append(headers, line.c_str());
    }

    if (!entry.last_modified_header.empty())
      headers = curl_slist_append(headers, ("If-Modified-Since: " + entry.last_modified_header).c_str());

    std::string resp_body, last_modified;

    curl_easy_setopt(request, CURLOPT_URL, url.c_str());
    curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(request, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(request, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(request, CURLOPT_HEADERDATA, &last_modified);
    curl_easy_setopt(request, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(request, CURLOPT_MAXREDIRS, 20);

    bool is_ok = false;
    do
    {
      CURLcode result = curl_easy_perform(request);
      if (result != CURLE_OK)
      {
          std::cerr << "curl: " << curl_easy_strerror(result) << std::endl;
          break;
      }

      long resp_code = 0;
      curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &resp_code);

      if (resp_code == 304)  // HTTP 304 Not Modified
      {
        entry.validated = cache::era();
        is_ok = true;
        break;
      }
      else if (resp_code != 200)  // HTTP 200 OK
      {
        std::cerr << "curl: HTTP " << resp_code << std::endl;
        break;
      }

      entry.modified = entry.validated = cache::era();
      entry.result.assign(resp_body);
      entry.last_modified_header.assign(last_modified);
      is_ok = true;
    } while(0);

    curl_slist_free_all(headers);
    curl_easy_cleanup(request);
    return is_ok;
}

#endif

} // UNNAMED NAMESPACE ====================================================

// http::set_proxy ==========================================================

void http::set_proxy( const std::string& proxy_type,
                      const std::string& proxy_host,
                      const unsigned     proxy_port )
{
  proxy.type = proxy_type;
  proxy.host = proxy_host;
  proxy.port = proxy_port;
}

// http::clear_cache ========================================================

bool http::clear_cache( sim_t* sim,
                        const std::string& name,
                        const std::string& value )
{
  assert( name == "http_clear_cache" ); ( void )name;
  if ( value != "0" && ! sim -> parent ) cache_clear();
  return true;
}

// http::cache_load =========================================================

namespace cache {
std::string get( std::istream& is )
{
  std::string result;
  while ( is )
  {
    unsigned char c = is.get();
    if ( ! c )
      break;
    result += c;
  }
  return result;
}

void put( std::ostream& os, const std::string& s )
{ os.write( s.c_str(), s.size() + 1 ); }

void put( std::ostream& os, const char* s )
{ os.write( s, std::strlen( s ) + 1 ); }
}

void http::cache_load( const std::string& file_name )
{
  auto_lock_t lock( cache_mutex );

  try
  {
    io::ifstream file;
    file.open( file_name, std::ios::binary );
    if ( !file ) return;
    file.exceptions( std::ios::eofbit | std::ios::failbit | std::ios::badbit );
    file.unsetf( std::ios::skipws );

    if ( cache::get( file ) != SC_VERSION )
    {
      // invalid version, GTFO
      return;
    }

    std::string content;
    content.reserve( 16 * 1024 );

    while ( ! file.eof() )
    {
      std::string url = cache::get( file );
      std::string last_modified = cache::get( file );

      uint32_t size;
      file.read( reinterpret_cast<char*>( &size ), sizeof( size ) );
      content.resize( size );
      file.read( &content[ 0 ], size );

      url_cache_entry_t& c = url_db[ url ];
      c.result = content;
      c.last_modified_header = last_modified;
      c.modified = c.validated = cache::IN_THE_BEGINNING;
    }
  }
  catch ( ... )
  {}
}

// http::cache_save =========================================================

void http::cache_save( const std::string& file_name )
{
  auto_lock_t lock( cache_mutex );

  try
  {
    io::ofstream file;
    file.open( file_name, std::ios::binary );
    if ( ! file ) return;
    file.exceptions( std::ios::eofbit | std::ios::failbit | std::ios::badbit );

    cache::put( file, SC_VERSION );

    for ( url_db_t::const_iterator p = url_db.begin(), e = url_db.end(); p != e; ++p )
    {
      if ( p -> second.validated == cache::INVALID_ERA )
        continue;

      cache::put( file, p -> first );
      cache::put( file, p -> second.last_modified_header );

      uint32_t size = as<uint32_t>( p -> second.result.size() );
      file.write( reinterpret_cast<const char*>( &size ), sizeof( size ) );
      file.write( p -> second.result.data(), size );
    }
  }
  catch ( ... )
  {}
}

// http::get ================================================================

bool http::get( std::string&       result,
                const std::string& url,
                const std::string& cleanurl,
                cache::behavior_e  caching,
                const std::string& confirmation )
{
  result.clear();

  std::string encoded_url = url;
  std::string encoded_clean_url = cleanurl;
  util::urlencode( encoded_url );
  util::urlencode( encoded_clean_url );

  auto_lock_t lock( cache_mutex );

  url_cache_entry_t& entry = url_db[ encoded_clean_url ];

  if ( HTTP_CACHE_DEBUG )
  {
    io::ofstream http_log;
    http_log.open( "simc_http_log.txt", std::ios::app );
    std::ostream::sentry s( http_log );
    if ( s )
    {
      http_log << cache::era() << ": get(\"" << cleanurl << "\") [";

      if ( entry.validated != cache::INVALID_ERA )
      {
        if ( entry.validated >= cache::era() )
          http_log << "hot";
        else if ( caching != cache::CURRENT )
          http_log << "warm";
        else
          http_log << "cold";
        http_log << ": (" << entry.modified << ", " << entry.validated << ')';
      }
      else
        http_log << "miss";
      if ( caching != cache::ONLY &&
           ( entry.validated == cache::INVALID_ERA ||
             ( caching == cache::CURRENT && entry.validated < cache::era() ) ) )
        http_log << " download";
      http_log << "]\n";
    }
  }

  if ( entry.validated < cache::era() && ( caching == cache::CURRENT || entry.validated == cache::INVALID_ERA ) )
  {
    if ( caching == cache::ONLY )
      return false;

    util::printf( "@" ); fflush( stdout );

    if ( ! download( entry, encoded_url ) )
      return false;

    if ( HTTP_CACHE_DEBUG && entry.modified < entry.validated )
    {
      io::ofstream http_log;
      http_log.open( "simc_http_log.txt", std::ios::app );
      http_log << cache::era() << ": Unmodified (" << entry.modified << ", " << entry.validated << ")\n";
    }

    if ( confirmation.size() && ( entry.result.find( confirmation ) == std::string::npos ) )
    {
      //util::printf( "\nsimulationcraft: HTTP failed on '%s'\n", url.c_str() );
      //util::printf( "%s\n", ( result.empty() ? "empty" : result.c_str() ) );
      //fflush( stdout );
      return false;
    }
  }

  result = entry.result;
  return true;
}

#ifdef UNIT_TEST

#include <iostream>

uint32_t dbc::get_school_mask( school_e ) { return 0; }
void sim_t::errorf( const char*, ... ) { }

int main( int argc, char* argv[] )
{
  if ( argc > 1 )
  {
    for ( int i = 1; i < argc; ++i )
    {
      if ( !strcmp( argv[ i ], "--dump" ) )
      {
        url_db.clear();
        const char* const url_cache_file = "simc_cache.dat";
        http::cache_load( url_cache_file );

        for ( auto& i : url_db )
        {
          std::cout << "URL: \"" << i.first << "\" (" << i.second.last_modified_header << ")\n"
                    << i.second.result << '\n';
        }
      }
      else
      {
        std::string result;
        if ( http::get( result, argv[ i ], cache::CURRENT ) )
          std::cout << result << '\n';
        else
          std::cout << "Unable to download \"" << argv[ i ] << "\".\n";
      }
    }
  }
  else
  {
    std::string result;

    if ( http::get( result, "http://us.battle.net/wow/en/character/llane/pagezero/advanced", cache::CURRENT ) )
      std::cout << result << '\n';
    else
      std::cout << "Unable to download armory data.\n";

    if ( http::get( result, "http://www.wowhead.com/list=1564664", cache::CURRENT ) )
      std::cout << result << '\n';
    else
      std::cout << "Unable to download wowhead data.\n";
  }

  return 0;
}

#endif
