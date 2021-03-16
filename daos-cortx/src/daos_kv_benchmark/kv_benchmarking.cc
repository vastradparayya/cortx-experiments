/**
 * Benchmarking tests for daos kv store.
 * Operation type    : put, get, list, remove.
 * Opeation number   : NR_OPS_XXXX per each opeation
 */
#include <benchmark/benchmark.h>
#include <cstdio>
#include <daos.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define POOL_ID "2bd513ad-66d1-4312-829b-d69d8637b455" /* pre-created pool uuid */

/* number of operations */
#define NR_OPS_100     100
#define NR_OPS_1000    1000
#define NR_OPS_10000   10000
#define NR_OPS_100000  100000
#define NR_OPS_1000000 1000000

/* number of query at a time while listing */
#define NR_KV_PER_LISTING 8

/* key sizes */
#define BM_KEY_64B   64
#define BM_KEY_128B  128
#define BM_KEY_256B  256
#define BM_KEY_512B  512
#define BM_KEY_1024B 1024

/* value sizes */
#define BM_VAL_1K  1024
#define BM_VAL_4K  (1024 * 4)
#define BM_VAL_8K  (1024 * 8)
#define BM_VAL_16K (1024 * 16)
#define BM_VAL_32K (1024 * 32)

/* buffer to hold keys while listing */
#define KEY_LIST_BUF (1024 * 1024)

#define ARG_KEY_SIZE_OPTIONS { BM_KEY_64B , BM_KEY_128B, BM_KEY_256B, BM_KEY_512B, BM_KEY_1024B }
#define ARG_VAL_SIZE_OPTIONS { BM_VAL_1K , BM_VAL_4K, BM_VAL_8K, BM_VAL_16K, BM_VAL_32K  }
#define NR_OPS_OPTIONS       {/* NR_OPS_1000, NR_OPS_10000, NR_OPS_10000, NR_OPS_100000*/ NR_OPS_1000000 }

#define ARG_MATRICS\
   { ARG_KEY_SIZE_OPTIONS, ARG_VAL_SIZE_OPTIONS, NR_OPS_OPTIONS }

#define DEBUG_LOG 0

#if DEBUG_LOG
#define LOG_MSG printf
#else
#define LOG_MSG //
#endif

static daos_handle_t poh; /* daos pool handle */
static daos_handle_t coh; /* daos container handle */

static char err_msg[ 128 ];

#define FAIL( fmt, ... )                                            \
    do {                                                            \
        fprintf(stderr, "Process (%s): " fmt " aborting\n",         \
                err_msg, ## __VA_ARGS__);                           \
        exit(1);                                                    \
    } while (0)

#define ASSERT( cond, ... )                                         \
    do {                                                            \
        if (!(cond))                                                \
        FAIL(__VA_ARGS__);                                          \
    } while (0)

daos_handle_t oh;  /* object handle */
daos_obj_id_t oid; /* daos object id */

uuid_t pool_uuid; /* pool uuid */
uuid_t co_uuid;   /* container uuid */

int setup_main( )
{
   int rc;

   /** initialize DAOS by connecting to local agent */
   rc = daos_init( );
   ASSERT( rc == 0, "daos_init failed with %d", rc );

   rc = uuid_parse( POOL_ID, pool_uuid );

   rc = daos_pool_connect( pool_uuid, NULL, DAOS_PC_RW, &poh,
                           NULL, NULL );
   ASSERT( rc == 0, "pool connect failed with %d", rc );

   /** generate uuid for container */
   uuid_generate( co_uuid );

   /** create container */
   rc = daos_cont_create( poh, co_uuid, NULL /* properties */,
                          NULL /* event */ );
   ASSERT( rc == 0, "container create failed with %d", rc );

   /** open container */
   rc = daos_cont_open( poh, co_uuid, DAOS_COO_RW, &coh, NULL,
                        NULL );
   ASSERT( rc == 0, "container open failed with %d", rc );

   oid.hi = 0;
   oid.lo = 4;

   /** the KV API requires the flat feature flag be set in the oid */
   daos_obj_generate_id( &oid, DAOS_OF_KV_FLAT, OC_SX, 0 );

   rc = daos_kv_open( coh, oid, DAOS_OO_RW, &oh, NULL );
   ASSERT( rc == 0, "KV open failed with %d", rc );
}

void tear_down( ) {
   int rc;

   /* close object handle */
   daos_kv_close( oh, NULL );

   /* close container */
   rc = daos_cont_close( coh, NULL );
   ASSERT( rc == 0, "cont close failed" );

   /* delete container */
   rc = daos_cont_destroy( poh, co_uuid, 1, NULL );
   ASSERT( rc == 0, "cont delete failed" );

   /* disconnect pool */
   rc = daos_pool_disconnect( poh, NULL );
   ASSERT( rc == 0, "disconnect failed" );

   /* teardown the DAOS stack */
   rc = daos_fini( );
   ASSERT( rc == 0, "daos_fini failed with %d", rc );
}

/**
 * @brief This function generates key name of key_size using number given number
 *        Key format generated by this function would be a string of characters
 *        of key_size which starts with 'x' and last 16 bytes will be a number
 *        'nr' with padding of zero before the number.
 *
 * @param key_buf    buffer to store key
 * @param nr         key number used to generate unique key
 * @param key_size   size of key
 * @return           none
 */
void gen_key_name( char *key_buf, int nr, int key_size )
{
   char key_name[ 20 ] = {
      0
   };

   /* generate different key */
   memset( key_buf, 'x', key_size - 1 );
   sprintf( key_name, "%.16d", nr );
   strncpy( ( char * )key_buf + strlen( key_buf ) - strlen( key_name ), ( char * )key_name, strlen( key_name ) );
}

/* Benchmarking function to test KV Put operation */
static void kv_put_function( benchmark::State &state ) {
   int rc;

   /* perform setup */
   setup_main( );

   unsigned int key_size = state.range( 0 ); // size of key
   unsigned int val_size = state.range( 1 ); // size of value for the given key
   unsigned int num_ops  = state.range( 2 ); // nr_ops is num of key-value pairs for the given test

   /* allocate key and value buffers */
   char *key_buf = ( char * )calloc( key_size, sizeof( char ) ); // key buffer allocated
   char *val_buf = ( char * )calloc( val_size, sizeof( char ) ); // value buffer allocated
   memset( val_buf, 'z', val_size - 1 );                         // populate with some random value.

   /* actual computation starts here */
   for ( auto _ : state )
   {
      /* call daos_kv_put for num_ops times */
      for ( int i = 0; i < num_ops; i++ )
      {
         state.PauseTiming( );

         /* generate different key */
         gen_key_name( key_buf, i, key_size );

         state.ResumeTiming( );

         /* actual function to mearsure time */
         daos_kv_put( oh, DAOS_TX_NONE, 0, ( char * )key_buf, val_size, val_buf, NULL );
      }
   }

   /* free resources */
   free( ( char * )key_buf );
   free( ( char * )val_buf );

   /* tear down */
   tear_down( );
}

/* Benchmarking function to test KV Get operation */
static void kv_get_function( benchmark::State &state ) {
   int rc;

   /* perform setup */
   setup_main( );

   unsigned int key_size = state.range( 0 ); // size of key
   unsigned int val_size = state.range( 1 ); // size of value for the given key
   unsigned int num_ops  = state.range( 2 ); // nr_ops is num of key-value pairs for the given test

   /* allocate key and value buffers */
   char *key_buf = ( char * )calloc( key_size, sizeof( char ) ); // key buffer allocated
   char *val_buf = ( char * )calloc( val_size, sizeof( char ) ); // value buffer allocated

   memset( val_buf, 'z', val_size - 1 );

   char *rbuf = ( char * )calloc( val_size, sizeof( char ) ); // rbuf to check value

   if ( rbuf == NULL )
      return;

   /* actual computation starts here */
   for ( auto _ : state )
   {
      /* call daos_kv_put for num_ops times */
      for ( int i = 0; i < num_ops; i++ )
      {
         state.PauseTiming( );

         /* generate different key */
         gen_key_name( key_buf, i, key_size );

         daos_kv_put( oh, DAOS_TX_NONE, 0, ( char * )key_buf, val_size, val_buf, NULL );

         daos_size_t size = 0;
         size = val_size;

         state.ResumeTiming( );

         /* actual function to mearsure time */
         daos_kv_get( oh, DAOS_TX_NONE, 0, key_buf, &size, rbuf, NULL );
      }
   }

   /* free resources */
   free( ( char * )key_buf );
   free( ( char * )rbuf );
   free( ( char * )val_buf );

   /* tear down */
   tear_down( );
}

/* Benchmarking function to test KV List operation */
static void kv_list_function( benchmark::State &state ) {
   /* perform setup */
   setup_main( );

   unsigned int key_size = state.range( 0 ); // size of key
   unsigned int val_size = state.range( 1 ); // size of value for the given key
   unsigned int num_ops  = state.range( 2 ); // nr_ops is num of key-value pairs for the given test
   unsigned int nr_query = NR_KV_PER_LISTING;

   /* allocate key and value buffers */
   char *key_buf = ( char * )calloc( key_size, sizeof( char ) ); // key buffer allocated
   char *val_buf = ( char * )calloc( val_size, sizeof( char ) ); // value buffer allocated
   memset( val_buf, 'z', val_size - 1 );                         // populate with some random value.

   char *rbuf = ( char * )calloc( val_size, sizeof( char ) ); // rbuf to check value

   /* call daos_kv_put for num_ops times */
   for ( int i = 0; i < num_ops; i++ )
   {
      /* generate different key */
      gen_key_name( key_buf, i, key_size );

      /* put keys and values */
      daos_kv_put( oh, DAOS_TX_NONE, 0, ( char * )key_buf, val_size, val_buf, NULL );
   }

   char            *list_of_keys;
   daos_key_desc_t kds[ NR_KV_PER_LISTING ];
   daos_anchor_t   anchor = {
      0
   };
   d_sg_list_t     sgl;
   d_iov_t         sg_iov;

   list_of_keys = ( char * )calloc( KEY_LIST_BUF, sizeof( char ) ); /* buffer to hold list of keys fetch in each list call */
   d_iov_set( &sg_iov, list_of_keys, KEY_LIST_BUF );
   sgl.sg_nr     = 1;
   sgl.sg_nr_out = 0;
   sgl.sg_iovs   = &sg_iov;

   /* actual computation starts here */
   for ( auto _ : state )
   {
      while ( !daos_anchor_is_eof( &anchor ) )
      {
         state.PauseTiming( );

         uint32_t nr = NR_KV_PER_LISTING;
         int      rc;

         memset( list_of_keys, 0, KEY_LIST_BUF );

         state.ResumeTiming( );

         rc = daos_kv_list( oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, \
                            NULL );
         ASSERT( rc == 0, "KV list failed with %d", rc );

         /* verify if returned number of descriptors are zero */
         if ( nr == 0 )
         {
            continue;
         }
         else // if returned descriptors are non zero then compute each key and query value.
         {
            unsigned int offset = 0;

            /* compute each key and fetch value */
            for ( int i = 0; i < nr; i++ )
            {
               state.PauseTiming( );

               memset( key_buf, 0, key_size );
               memset( rbuf, 0, val_size );

               /* obtain key_buf value from sgl.sg_iovs */

               LOG_MSG( "key size is : %d\n", kds[ i ].kd_key_len );
               memcpy( key_buf, ( char * )( ( sgl.sg_iovs )->iov_buf ) + offset, kds[ i ].kd_key_len );

               /* update offset for next key */
               offset += kds[ i ].kd_key_len;

               state.ResumeTiming( );

               /* compute value for the key_buf */
               daos_size_t size = 0;
               size = val_size;
               daos_kv_get( oh, DAOS_TX_NONE, 0, key_buf, &size, rbuf, NULL );
            }
         }
      }

      free( ( char * )list_of_keys );
   }

   /* free resources */
   free( ( char * )key_buf );
   free( ( char * )val_buf );
   free( ( char * )rbuf );

   /* tear down */
   tear_down( );
}

/* Benchmarking function to test KV Remove operation */
static void kv_remove_function( benchmark::State &state ) {
   int rc;

   /* perform setup */
   setup_main( );

   unsigned int key_size = state.range( 0 ); // size of key
   unsigned int val_size = state.range( 1 ); // size of value for the given key
   unsigned int num_ops  = state.range( 2 ); // nr_ops is num of key-value pairs for the given test

   /* allocate key and value buffers */
   char *key_buf = ( char * )calloc( key_size, sizeof( char ) ); // key buffer allocated
   char *val_buf = ( char * )calloc( val_size, sizeof( char ) ); // value buffer allocated

   memset( val_buf, 'z', val_size - 1 );

   /* actual computation starts here */
   for ( auto _ : state )
   {
      /* call daos_kv_put for num_ops times */
      for ( int i = 0; i < num_ops; i++ )
      {
         state.PauseTiming( );

         /* generate different key */
         gen_key_name( key_buf, i, key_size );

         /* put keys */
         daos_kv_put( oh, DAOS_TX_NONE, 0, ( char * )key_buf, val_size, val_buf, NULL );

         state.ResumeTiming( );

         /* actual function to mearsure time */
         daos_kv_remove( oh, DAOS_TX_NONE, 0, key_buf, NULL );
      }
   }

   /* free resources */
   free( key_buf );

   /* tear down */
   tear_down( );
}

/* Put keys */
BENCHMARK( kv_put_function )
->ArgsProduct( ARG_MATRICS )
->Iterations( 1 )
->Unit( benchmark::kMillisecond );

/* Get keys */
BENCHMARK( kv_get_function )
->ArgsProduct( ARG_MATRICS )
->Iterations( 1 )
->Unit( benchmark::kMillisecond );

/* list keys */
BENCHMARK( kv_list_function )
->ArgsProduct( ARG_MATRICS )
->Iterations( 1 )
->Unit( benchmark::kMillisecond );

/* remove keys */
BENCHMARK( kv_remove_function )
->ArgsProduct( ARG_MATRICS )
->Iterations( 1 )
->Unit( benchmark::kMillisecond );

/* Run the benchmark */
BENCHMARK_MAIN( );