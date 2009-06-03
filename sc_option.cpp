// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simcraft.h"

namespace { // ANONYMOUS NAMESPACE ==========================================

// is_white_space ===========================================================

static bool is_white_space( char c )
{
  return( c == ' ' || c == '\t' || c == '\n' || c == '\r' );
}

// only_white_space =========================================================

static bool only_white_space( char* s )
{
  while ( *s )
  {
    if ( ! is_white_space( *s ) )
      return false;
    s++;
  }
  return true;
}

// remove_space =============================================================

static void remove_white_space( std::string& buffer,
                                char*        line )
{
  while ( is_white_space( *line ) ) line++;

  char* endl = line + strlen( line ) - 1;

  while ( endl > line && is_white_space( *endl ) )
  {
    *endl = '\0';
    endl--;
  }

  buffer = line;
}

} // ANONYMOUS NAMESPACE ====================================================

// option_t::print ==========================================================

void option_t::print( FILE* file )
{
  if( type == OPT_STRING ||
      type == OPT_STRING_Q )
  {
    std::string& v = *( ( std::string* ) address );
    fprintf( file, "%s=%s\n", name, v.empty() ? "" : v.c_str() );
  }
  else if( type == OPT_CHARP ||
	   type == OPT_CHARP_Q )
  {
    const char* v = *( ( char** ) address );
    fprintf( file, "%s=%s\n", name, v ? v : "" );
  }
  else if( type == OPT_BOOL ||
	   type == OPT_BOOL_Q )
  {
    int v = *( ( int* ) address );
    fprintf( file, "%s=%d\n", name, (v>0) ? 1 : 0 );
  }
  else if( type == OPT_INT ||
	   type == OPT_INT_Q )
  {
    int v = *( ( int* ) address );
    fprintf( file, "%s=%d\n", name, v );
  }
  else if( type == OPT_FLT ||
	   type == OPT_FLT_Q )
  {
    double v = *( ( double* ) address );
    fprintf( file, "%s=%.2f\n", name, v );
  }
  else if( type == OPT_LIST )
  {
    std::vector<std::string>& v = *( ( std::vector<std::string>* ) address );
    fprintf( file, "%s=", name );
    for ( unsigned i=0; i < v.size(); i++ ) fprintf( file, "%s%s", ( i?" ":"" ), v[ i ].c_str() );
    fprintf( file, "\n" );
  }
}

// option_t::save ===========================================================

void option_t::save( FILE* file )
{
  if( type == OPT_STRING )
  {
    std::string& v = *( ( std::string* ) address );
    if( ! v.empty() ) fprintf( file, "%s=%s\n", name, v.c_str() );
  }
  else if( type == OPT_CHARP )
  {
    const char* v = *( ( char** ) address );
    if( v ) fprintf( file, "%s=%s\n", name, v );
  }
  else if( type == OPT_BOOL )
  {
    int v = *( ( int* ) address );
    if( v > 0 ) fprintf( file, "%s=1\n", name );
  }
  else if( type == OPT_INT )
  {
    int v = *( ( int* ) address );
    if( v != 0 ) fprintf( file, "%s=%d\n", name, v );
  }
  else if( type == OPT_FLT )
  {
    double v = *( ( double* ) address );
    if( v != 0 ) fprintf( file, "%s=%.2f\n", name, v );
  }
  else if( type == OPT_LIST )
  {
    std::vector<std::string>& v = *( ( std::vector<std::string>* ) address );
    if( ! v.empty() )
    {
      fprintf( file, "%s=", name );
      for ( unsigned i=0; i < v.size(); i++ ) fprintf( file, "%s%s", ( i?" ":"" ), v[ i ].c_str() );
      fprintf( file, "\n" );
    }
  }
}

// option_t::copy ===========================================================

void option_t::copy( std::vector<option_t>& opt_vector,
		     option_t*              opt_array )
{
  int vector_size = opt_vector.size();
  int  array_size = 0;

  for( int i=0; opt_array[ i ].name; i++ ) array_size++;
  opt_vector.resize( vector_size + array_size );

  for( int i=0; i < array_size; i++ )
  {
    opt_vector[ vector_size + i ]  = opt_array[ i ];
  }  
}

// option_t::parse ==========================================================

bool option_t::parse( sim_t*             sim,
                      const std::string& n,
                      const std::string& v )
{
  if ( n == name )
  {
    switch ( type )
    {
    case OPT_STRING:
    case OPT_STRING_Q: *( ( std::string* ) address ) = v;                         break;
    case OPT_APPEND:   *( ( std::string* ) address ) += v;                        break;
    case OPT_CHARP:   
    case OPT_CHARP_Q:  *( ( char** )       address ) = util_t::dup( v.c_str() );  break;
    case OPT_INT:      
    case OPT_INT_Q:    *( ( int* )         address ) = atoi( v.c_str() );         break;
    case OPT_FLT:    
    case OPT_FLT_Q:    *( ( double* )      address ) = atof( v.c_str() );         break;
    case OPT_BOOL:
    case OPT_BOOL_Q:
      *( ( int* ) address ) = atoi( v.c_str() ) ? 1 : 0;
      if ( v != "0" && v != "1" ) printf( "simcraft: Acceptable values for '%s' are '1' or '0'\n", name );
      break;
    case OPT_FUNC: return ( (option_function_t) address )( sim, n, v );
    case OPT_LIST:   ( ( std::vector<std::string>* ) address ) -> push_back( v ); break;
    case OPT_DEPRECATED:
      printf( "simcraft: option '%s' has been deprecated.\n", name );
      if ( address ) printf( "simcraft: please use '%s' instead.\n", ( char* ) address );
      exit( 0 );
    default: assert( 0 );
    }
    return true;
  }

  return false;
}

// option_t::parse ==========================================================

bool option_t::parse( sim_t*                 sim,
		      std::vector<option_t>& options,
                      const std::string&     name,
                      const std::string&     value )
{
  int num_options = options.size();

  for ( int i=0; i < num_options; i++ )
    if( options[ i ].parse( sim, name, value ) )
      return true;

  return false;
}

// option_t::parse ==========================================================

bool option_t::parse( sim_t*             sim,
		      option_t*          options,
                      const std::string& name,
                      const std::string& value )
{
  for ( int i=0; options[ i ].name; i++ )
    if( options[ i ].parse( sim, name, value ) )
      return true;

  return false;
}

// option_t::parse_file =====================================================

bool option_t::parse_file( sim_t* sim,
                           FILE*  file )
{
  char buffer[ 1024 ];
  while ( fgets( buffer, 1024, file ) )
  {
    if ( *buffer == '#' ) continue;
    if ( only_white_space( buffer ) ) continue;
    option_t::parse_line( sim, buffer );
  }
  return true;
}

// option_t::parse_line =====================================================

bool option_t::parse_line( sim_t* sim,
                           char*  line )
{
  std::string buffer;

  remove_white_space( buffer, line );

  std::vector<std::string> tokens;

  int num_tokens = util_t::string_split( tokens, buffer, " \t\n" );

  for ( int i=0; i < num_tokens; i++ )
    if ( ! parse_token( sim, tokens[ i ] ) )
      return false;

  return true;
}

// option_t::parse_token ====================================================

bool option_t::parse_token( sim_t*       sim,
                            std::string& token )
{
  if ( token == "-" )
  {
    parse_file( sim, stdin );
    return true;
  }

  std::string::size_type cut_pt = token.find_first_of( "=" );

  if ( cut_pt == token.npos )
  {
    FILE* file = fopen( token.c_str(), "r" );
    if ( ! file )
    {
      printf( "simcraft: Unexpected parameter '%s'.  Expected format: name=value\n", token.c_str() );
    }
    parse_file( sim, file );
    fclose( file );
    return true;
  }

  std::string name, value;

  name  = token.substr( 0, cut_pt );
  value = token.substr( cut_pt + 1 );

  if ( name == "input" )
  {
    FILE* file = fopen( value.c_str(), "r" );
    if ( ! file )
    {
      printf( "simcraft: Unable to open input parameter file '%s'\n", value.c_str() );
    }
    parse_file( sim, file );
    fclose( file );
  }
  else if ( ! sim -> parse_option( name, value ) )
  {
    printf( "simcraft: Unknown option/value pair: '%s' : '%s'\n", name.c_str(), value.c_str() );
    return false;
  }

  return true;
}
