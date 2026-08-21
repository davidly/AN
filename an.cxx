#define _CRT_SECURE_NO_WARNINGS

// strlwr triggers MSVC's separate "POSIX name deprecated" warning (C4996), which
// _CRT_SECURE_NO_WARNINGS above doesn't cover -- that one's for the "less safe function"
// category (strcpy/sprintf/etc). This macro is the correct, minimal suppression for the
// POSIX-name category, and it's a no-op on non-MSVC compilers, so it doesn't affect the
// g++/clang++ build at all.

#define _CRT_NONSTDC_NO_DEPRECATE

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>

#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <thread>

using namespace std;
using namespace std::chrono;

#ifdef _MSC_VER

    #define USE_PPL
    #include <ppl.h>
    using namespace concurrency;

    // Don't include windows.h just for this one API that has no equivalent in the standard C++ runtime

    extern "C" unsigned long __stdcall GetModuleFileNameA( void * module, char * filename, unsigned long size );
    #define PATH_MAX 300

#pragma warning( disable: 4100 ) // unreferenced formal parameter
    char * realpath( char * exepath, char * output )
    {
        // ignore exepath since NULL refers to that module

        GetModuleFileNameA( NULL, output, PATH_MAX );

        return output;
    } //realpath
#pragma warning( default: 4100 ) // unreferenced formal parameter

#else // likely G++ / Clang++

    //#define __forceinline __attribute__((always_inline))     // this generates useless warnings in g++ that I can't turn off.
    #define __forceinline

    #ifndef __min
        #define __min( a, b ) ( a < b ) ? a : b
    #endif

    #ifndef _countof
        template < typename T, size_t N > size_t _countof( T ( & arr )[ N ] ) { return std::extent< T[ N ] >::value; }
    #endif

    char * strlwr( char * s )
    {
        char * orig = s;
        while ( *s )
        {
            *s = tolower( *s );
            s++;
        }
        return orig;
    }

#endif

class CAnagramSet;
class CRealDictionary;

const int MaxAnStringLen = 60;
CAnagramSet * g_Results = NULL;
int g_LongestWord = 0;
CRealDictionary * g_SortedWords = NULL;
bool g_IncrementalProgress = false;
bool g_OneThread = false;

// Set UseInternal to true to use memcmp and memcpy, but that's slower for both Windows and linux

#define UseInternalMemFunctions false

__forceinline void my_memcpy( void * a, void * b, int c )
{
#if UseInternalMemFunctions
    memcpy( a, b, c );
#else
    char *pa = (char *) a;
    char *pb = (char *) b;

    while ( c > 0 )
    {
        *pa++ = *pb++;
        c--;
    }
#endif
} //my_memcpy

class CFile
{
    public:
        FILE * fp;

        CFile( FILE * file )
        {
            fp = file;
        }

        ~CFile()
        {
            Close();
        }

        void Close()
        {
            if ( NULL != fp )
            {
                fclose( fp );
                fp = NULL;
            }
        }
};

class CAnString
{
    public:
        int len;
        char buf[ MaxAnStringLen + 1 ];

        CAnString( char * p, int c )
        {
            len = c;
            my_memcpy( buf, p, c + 1 );
        }

        CAnString( char * p )
        {
            len = (int) strlen( p );
            my_memcpy( buf, p, len + 1 );
        }

        CAnString()
        {
            len = 0;
            buf[ 0 ] = 0;
        }

        CAnString( int l )
        {
            len = l;
            buf[ l ] = 0;
        }

        void Set( char * p, int lens )
        {
            len = lens;
            my_memcpy( buf, p, 1 + lens );
        }

        void Set( CAnString * p )
        {
            len = p->len;
            my_memcpy( buf, p->buf, 1 + len );
        }
}; //CAnString

class CBTEntry
{
    public:
        CBTEntry * left;
        CBTEntry * right;
        CAnString words;

        CBTEntry( char * s, int lens )
        {
            left = NULL;
            right = NULL;
            words.Set( s, lens );
        }
}; //CBTEntry

__forceinline int Compare( CAnString * a, CAnString * b )
{
    int l = __min( a->len, b->len );

#if UseInternalMemFunctions
    int c = memcmp( a->buf, b->buf, l );

    if ( 0 != c )
        return c;
#else
    char * pa = a->buf;
    char * pb = b->buf;
    char * pastpa = pa + l;

    do
    {
        int c = *pa - *pb;
        if ( 0 != c )
            return c;
        pa++;
        pb++;
    } while ( pa < pastpa );
#endif

    return a->len - b->len;
} //Compare

__forceinline int Compare( char * a, int lena, CAnString * b )
{
    int l = __min( lena, b->len );

#if UseInternalMemFunctions
    int c = memcmp( a, b->buf, l );

    if ( 0 != c )
        return c;
#else
    char * pa = a;
    char * pb = b->buf;
    char * pastpa = pa + l;

    do
    {
        int c = *pa - *pb;
        if ( 0 != c )
            return c;
        pa++;
        pb++;
    } while ( pa < pastpa );
#endif

    return lena - b->len;
} //Compare

__forceinline bool Same( CAnString * a, CAnString * b )
{
    if ( a->len != b->len )
        return false;

#if UseInternalMemFunctions
    return ( 0 == memcmp( a->buf, b->buf, a->len ) );
#else
    for ( int i = 0; i < a->len; i++ )
        if ( a->buf[ i ] != b->buf[ i ] )
            return false;

    return true;
#endif
}

class CBinaryTree
{
    private:
        CBTEntry * root;
        int count;
        std::mutex mtx;

    public:
        CBinaryTree()
        {
            root = NULL;
            count = 0;
        }

        void FreeTree( CBTEntry * p )
        {
            if ( NULL != p->left )
            {
                FreeTree( p->left );
                p->left = NULL;
            }

            if ( NULL != p->right )
            {
                FreeTree( p->right );
                p->right = NULL;
            }

            delete p;
        }

        ~CBinaryTree()
        {
            if ( NULL != root )
            {
                FreeTree( root );
                root = NULL;
            }
        }

        int Count() { return count; }

        bool Add( char * s, int lens )
        {
            lock_guard<mutex> lock( mtx );
            {
                if ( NULL == root )
                {
                    root = new CBTEntry( s, lens );
                    count++;
                    return true;
                }
    
                CBTEntry * e = root;
    
                do
                {
                    int c = Compare( s, lens, & e->words );
    
                    if ( 0 == c )
                        return false;
    
                    if ( c < 0 )
                    {
                        if ( NULL == e->left )
                        {
                            e->left = new CBTEntry( s, lens );
                            count++;
                            return true;
                        }
    
                        e = e->left;
                    }
                    else
                    {
                        if ( NULL == e->right )
                        {
                            e->right = new CBTEntry( s, lens );
                            count++;
                            return true;
                        }
    
                        e = e->right;
                    }
                } while ( true );
            }
        }

        bool IsEmpty()
        {
            return ( NULL == root );
        } //IsEmpty

        void Fill( CBTEntry * p, int * pCurrent, CAnString ** elements )
        {
            elements[ *pCurrent ] = & p->words;
            ( *pCurrent )++;

            if ( NULL != p->left )
                Fill( p->left, pCurrent, elements );

            if ( NULL != p->right )
                Fill( p->right, pCurrent, elements );
        }

        void Fill( int * pCurrent, CAnString ** elements )
        {
            if ( NULL != root )
                Fill( root, pCurrent, elements );
        }

}; //CBinaryTree

// The shared result set (g_Results) is inserted into concurrently from every worker
// thread, each insertion holding its shard's mutex for the full tree walk. A fixed 7
// shards was fine for the core counts common when this was written, but undersizes on
// today's much-higher core counts, where it becomes real lock contention. Scale with the
// hardware instead, with 7 as a floor so low-core machines are unaffected.

int DefaultAnagramSetTreeCount()
{
    unsigned int hc = std::thread::hardware_concurrency();
    return ( hc > 7 ) ? (int) hc : 7;
} //DefaultAnagramSetTreeCount

class CAnagramSet
{
    private:
        CBinaryTree ** pTrees;
        int treeCount;

    public:
        CAnagramSet( int c = DefaultAnagramSetTreeCount() )
        {
            treeCount = c;
    
            pTrees = new CBinaryTree * [ c ];
            memset( pTrees, 0, c * sizeof ( CBinaryTree * ) );
    
            Clear();
        }
    
        ~CAnagramSet()
        {
            for ( int i = 0; i < treeCount; i++ )
            {
                if ( NULL != pTrees[ i ] )
                {
                    delete pTrees[ i ];
                    pTrees[ i ] = 0;
                }
            }
    
            delete [] pTrees;
            pTrees = NULL;
        }
    
        int Count()
        {
            int count = 0;
            for ( int i = 0; i < treeCount; i++ )
                count += pTrees[ i ]->Count();
    
            return count;
        }
    
        bool IsEmpty()
        {
            for ( int i = 0; i < treeCount; i++ )
                if ( 0 != pTrees[ i ]->Count() )
                    return false;
    
            return true;
        }
    
        bool Add( char * s, int lens )
        {
            CBinaryTree * p;

            if ( 1 == treeCount )
            {
                p = pTrees[ 0 ];
            }
            else
            {
                unsigned long hcode = GetHashCode( s, lens );
                p = pTrees[ hcode % treeCount ];
            }
    
            return p->Add( s, lens );
        }
    
        unsigned long GetHashCode( char * s, int lens )
        {
            unsigned long h = (unsigned long) lens;
    
            for ( int i = 0; i < lens; i++ )
            {
                h ^= 3u * s[ i ];
                h <<= 3;
            }
    
            return h;
        } //GetHashCode
    
        void Clear()
        {
            for ( int i = 0; i < treeCount; i++ )
            {
                if ( NULL != pTrees[ i ] )
                {
                    delete pTrees[ i ];
                    pTrees[ i ] = 0;
                }
    
                pTrees[ i ] = new CBinaryTree();
            }
        }
    
        void GetAnagrams( CAnString ** array, int count )
        {
            int sofar = 0;
    
            for ( int i = 0; i < treeCount; i++ )
                pTrees[ i ]->Fill( &sofar, array );

            assert( sofar == count );
        }
}; //CAnagramSet

class CWordEntry
{
    public:
        char * word;
        CWordEntry * next;

        CWordEntry( char * str, int len )
        {
            word = new char[ len + 1 ];
            strcpy( word, str );
            next = NULL;
        }
}; //CWordEntry

// A trie over the dictionary's sorted-letter signatures. Since every key inserted is
// itself sorted, any root-to-node path visits letters in non-decreasing order, so the
// trie's own structure is exactly the set of distinct sorted letter-multisets that some
// dictionary word actually has -- walking it with a remaining-letter budget (see
// SearchTrieNode) only ever visits combinations that could possibly be a real word,
// instead of generating every combination and then checking.

class CTrieNode
{
    public:
        CTrieNode * children[ 26 ];
        CWordEntry * words;   // non-NULL if this node's path is itself a dictionary signature

        CTrieNode()
        {
            memset( children, 0, sizeof( children ) );
            words = NULL;
        }
}; //CTrieNode

class CTrie
{
    private:
        CTrieNode root;

    public:
        void Insert( CAnString * sortedSignature, CWordEntry * headEntry )
        {
            CTrieNode * node = &root;

            for ( int i = 0; i < sortedSignature->len; i++ )
            {
                int c = sortedSignature->buf[ i ] - 'a';
                if ( NULL == node->children[ c ] )
                    node->children[ c ] = new CTrieNode();

                node = node->children[ c ];
            }

            node->words = headEntry;
        } //Insert

        CTrieNode * Root() { return &root; }
}; //CTrie

CTrie * g_Trie = NULL;

class CDictionaryEntry
{
    public:
        CAnString sortedWord;
        CWordEntry * entries;
        CDictionaryEntry * next;

        CDictionaryEntry( CAnString * input, CWordEntry * pWordEntry, CDictionaryEntry * pDictionaryEntry )
        {
            sortedWord.Set( input );
            entries = pWordEntry;
            next = pDictionaryEntry;
        }
}; //CDictionaryEntry

class CRealDictionary
{
    private:
        CDictionaryEntry ** entries;
        static const int dictionaryHashSize = 1000003;

        __forceinline unsigned long long GetHashCode( CAnString * s )
        {
            int len = s->len;
            unsigned long long h = (unsigned long long) len;

            int i = 0;
            do
            {
                char x = s->buf[ i++ ] - 19;
                h ^= 3u * x;
                h <<= 4;
            } while ( i < len );
    
            return h;
        } //GetHashCode

    public:
        CRealDictionary()
        {
            entries = new CDictionaryEntry * [ dictionaryHashSize ];
            memset( entries, 0, dictionaryHashSize * sizeof ( CDictionaryEntry * ) );
        }

        int GetEntry( CAnString * input )
        {
            return (int) ( GetHashCode( input ) % ( unsigned long long) dictionaryHashSize );
        } //GetEntry

        void Add( CAnString * input, CWordEntry * p, int entry )
        {
            CDictionaryEntry * pEntry = entries[ entry ];

            CDictionaryEntry * newEntry = new CDictionaryEntry( input, p, pEntry );

            entries[ entry ] = newEntry;
        } //Add

        void Add( CAnString * input, CWordEntry * p )
        {
            Add( input, p, GetEntry( input ) );
        } //Add

        bool TryGetValue( CAnString * input, CWordEntry ** p, int entry )
        {
            CDictionaryEntry * pEntry = entries[ entry ];

            while ( NULL != pEntry )
            {
                if ( Same( & pEntry->sortedWord, input ) )
                {
                    *p = pEntry->entries;
                    return true;
                }

                pEntry = pEntry->next;
            }

            return false;
        } //TryGetValue

        bool TryGetValue( CAnString * input, CWordEntry ** p )
        {
            return TryGetValue( input, p, GetEntry( input ) );
        } //TryGetValue

        void Stats( int &longestChain, int &unused, int &moreThan5, int &collisions, int &singles )
        {
            moreThan5 = 0;
            longestChain = 0;
            collisions = 0;
            singles = 0;

            for ( int i = 0; i < dictionaryHashSize; i++ )
            {
                CDictionaryEntry *entry = entries[ i ];

                if ( NULL == entry )
                    unused++;
                else
                {
                    int count = 1;

                    entry = entry->next;

                    while ( NULL != entry )
                    {
                        count++;
                        entry = entry->next;
                    }

                    if ( count > 5 )
                        moreThan5++;

                    if ( 1 == count )
                        singles++;

                    if ( count > longestChain )
                    {
                        longestChain = count;

                        CDictionaryEntry *e = entries[ i ];

                        printf( "chain of %d hash %d\n", count, i );
                        while ( NULL != e )
                        {
                            printf( " '%s' (%d) ", e->sortedWord.buf, e->sortedWord.len );
                            e = e->next;
                        }

                        printf( "\n" );
                    }

                    if ( count > 1 )
                        collisions++;
                }
            }
        } //Stats
}; //CRealDictionary

int SpaceCount( CAnString * p )
{
    int spaces = 0;

    for ( int i = 0; i < p->len; i++ )
        if ( ' ' == p->buf[ i ] )
            spaces++;

    return spaces;
} //SpaceCount

class CStringBuilder
{
    private:
        char * pbuf;
        int allocated;
        int used;

    public:
        CStringBuilder( int initialSize = 100 )
        {
            allocated = initialSize;
            pbuf = new char[ allocated ];
            used = 0;
        }

        ~CStringBuilder()
        {
            delete [] pbuf;
        }

        void Append( CAnString * p, int offset, int count )
        {
            EnsureSize( count );
            my_memcpy( pbuf + used, p->buf + offset, count );
            used += count;
        }

        void Append( char * p, int len )
        {
            EnsureSize( len );
            my_memcpy( pbuf + used, p, len );
            used += len;
        }

        void Append( CAnString * p )
        {
            Append( p->buf, p->len );
        }

        void Append( char * p )
        {
            Append( p, (int) strlen( p ) );
        }

        void Append( char c )
        {
            EnsureSize( 1 );
            pbuf[ used++ ] = c;
        }

        void Append( char c, int count )
        {
            EnsureSize( count );

            for ( int x = 0; x < count; x++ )
                pbuf[ used++ ] = c;
        }

        void EnsureSize( int touse )
        {
            int request = touse + used;

            // >= to reserve space for a null at the end.

            if ( request >= allocated )
            {
                do
                {
                    allocated *= 2;
                } while ( allocated <= request );

                char * pnew = new char[ allocated ];
                my_memcpy( pnew, pbuf, used );
                delete [] pbuf;
                pbuf = pnew;
            }
        } //EnsureSize

        char * ToString( int & len )
        {
            // the caller doesn't own the string and it's only valid until the next call into this object.

            len = used;
            pbuf[ used ] = 0;
            return pbuf;
        } //ToString

        void Reset()
        {
            used = 0;
        }

        int Length() { return used; }

        void SetLength( int length )
        {
            // This function is just very trusting
            assert( length < allocated );

            used = length;
        }
}; //CStringBuilder

int FindSpace( CAnString * p, int offset )
{
    int o = offset;

    while ( o < p->len )
    {
        if ( ' ' == p->buf[ o ] )
            return o;

        o++;
    }

    return -1;
} //FindSpace

int Compare( char * a, int aLength, char * b, int bOffset, int bLength )
{
    int o = 0;
    int l = __min( aLength, bLength );

    while ( o < l )
    {
        if ( a[ o ] != b[ o + bOffset ] )
            return a[ o ] - b[ o + bOffset ];

        o++;
    }

    return aLength - bLength;
} //Compare

void AddAnagram( CAnagramSet * set, char * pAnagram, int len )
{
    bool fAdded = set->Add( pAnagram, len );

    if ( g_IncrementalProgress && ( set == g_Results ) && fAdded )
        printf( "%s\n", pAnagram );
} //AddAnagram

void FindPhraseAnagramsTrie( CAnagramSet * set, int * remaining, int totalLen, int anagramWords, bool allowParallel = true );

// Looks up a letter-count budget directly in the dictionary trie, without ever
// materializing it as a string. Profiling showed ProcessLeftMatch's own code -- mostly the
// old "build a right CAnString from remaining[], then hash it" sequence -- was 33% of
// total runtime despite only ~9% of its 24M calls ever finding a match; walking the trie
// straight from remaining[] skips the string build entirely and short-circuits the moment
// a required letter has no corresponding child, instead of always building and hashing the
// whole thing first regardless of whether it was ever going to match.

CWordEntry * TrieLookup( int * remaining )
{
    CTrieNode * node = g_Trie->Root();

    for ( int c = 0; c < 26; c++ )
    {
        int k = remaining[ c ];
        while ( k > 0 )
        {
            node = node->children[ c ];
            if ( NULL == node )
                return NULL;
            k--;
        }
    }

    return node->words;
} //TrieLookup

// Called once a trie walk lands on a node whose path is itself a dictionary signature
// (listL) at depth `len` -- i.e. the same situation the old code reached after a Combo
// combination happened to hash-match. Everything from here down (look up the complementary
// right side as a single word, and/or recurse for 3+-word phrases with alphabetically-
// sorted word insertion) is unchanged from the original combinatorial implementation;
// only how a "left" match gets found -- and now, how "right" gets looked up -- changed.

void ProcessLeftMatch( CAnagramSet * set, int totalLen, int anagramWords, CWordEntry * listL, int len, int * remaining )
{
    int rightlen = totalLen - len;

    unique_ptr<CStringBuilder> sb;
    unique_ptr<CAnagramSet> rightSet;

    CWordEntry * listR = TrieLookup( remaining );

    if ( NULL != listR )
    {
        sb.reset( new CStringBuilder() );

        CWordEntry * sL = listL;

        do
        {
            assert( len == strlen( sL->word ) );

            CWordEntry * sR = listR;

            do
            {
                assert( rightlen == strlen( sR->word ) );

                int c = strcmp( sL->word, sR->word );
                sb->Reset();

                if ( c < 0 )
                {
                    sb->Append( sL->word, len );
                    sb->Append( ' ' );
                    sb->Append( sR->word, rightlen );
                }
                else
                {
                    sb->Append( sR->word, rightlen );
                    sb->Append( ' ' );
                    sb->Append( sL->word, len );
                }

                int lenAnagram = 0;
                char * pAnagram = sb->ToString( lenAnagram );
                AddAnagram( set, pAnagram, lenAnagram );

                sR = sR->next;
            } while ( NULL != sR );

            sL = sL->next;
        } while ( NULL != sL );
    }

    if ( ( anagramWords > 2 ) && ( rightlen > 1 ) )
    {
        rightSet.reset( new CAnagramSet( 1 ) );

        // ProcessLeftMatch only ever runs from inside SearchTrieNode, which is itself
        // already running inside the top-level parallel region (or one of its worker
        // threads) -- so this recursive call is always nested. Both PPL and OpenMP treat
        // parallelism from within an already-active parallel context as a no-op (OpenMP
        // explicitly disables nested parallel regions by default, running them on a team
        // of 1), so attempting to parallelize here would just pay region-entry overhead
        // for every single match found, at every recursion depth, for zero benefit --
        // confirmed by instrumenting the OpenMP build directly (omp_get_level()/
        // omp_in_parallel() showed exactly this on every recursive call).

        // Safe to pass `remaining` through directly rather than copying it: this whole
        // recursive call runs sequentially on the current thread (allowParallel=false, so
        // CollectWorkUnits's own decrement/recurse/increment backtracking never overlaps
        // with anything else touching this array), and nothing later in this function
        // still needs `remaining` for its own purposes -- only rightSet's own found
        // anagram-phrase strings (via `array`/`anagram` below) matter from here on.

        FindPhraseAnagramsTrie( rightSet.get(), remaining, rightlen, anagramWords - 1, false );

        if ( !rightSet->IsEmpty() )
        {
            if ( nullptr == sb.get() )
                sb.reset( new CStringBuilder() );

            int cAnagrams = rightSet->Count();
            unique_ptr<CAnString * []> array ( new CAnString *[ cAnagrams ] );
            rightSet->GetAnagrams( array.get(), cAnagrams );

            for ( int a = 0; a < cAnagrams; a++ )
            {
                CAnString * anagram = array.get()[ a ];
                int spaces = SpaceCount( anagram );

                int wordEnds[ MaxAnStringLen ];
                {
                    int off = 0;
                    for ( int wi = 0; wi <= spaces; wi++ )
                    {
                        int sp = FindSpace( anagram, off );
                        if ( -1 == sp )
                            sp = anagram->len;
                        wordEnds[ wi ] = sp;
                        off = sp + 1;
                    }
                }

                CWordEntry * sL = listL;
                assert( len == strlen( sL->word ) );

                do
                {
                    sb->Reset();
                    int wordsRight = 1 + spaces;
                    int offset = 0;
                    int wordIdx = 0;

                    do
                    {
                        int sp = wordEnds[ wordIdx++ ];

                        int comp = Compare( sL->word, len, anagram->buf, offset, sp - offset );

                        if ( comp < 0 )
                        {
                            sb->Append( anagram, 0, offset );
                            sb->Append( sL->word, len );
                            sb->Append( ' ' );
                            sb->Append( anagram, offset, anagram->len - offset );

                            break;
                        }

                        wordsRight--;
                        offset = sp + 1;
                    } while ( wordsRight > 0 );

                    if ( 0 == sb->Length() )
                    {
                        sb->Append( anagram );
                        sb->Append( ' ' );
                        sb->Append( sL->word, len );
                    }

                    int lenAnagram = 0;
                    char * pAnagram = sb->ToString( lenAnagram );
                    AddAnagram( set, pAnagram, lenAnagram );

                    sL = sL->next;
                } while ( NULL != sL );
            }

            rightSet->Clear();
        }
    }
} //ProcessLeftMatch

// Depth-first walk of the dictionary trie, carrying a remaining-letter budget (how many
// of each letter from `input` are still unused along this path). The trie's own structure
// means every node reachable here corresponds to a prefix of some real dictionary word's
// sorted signature, so unlike Combo, this never visits a combination that couldn't
// possibly be a match, and -- since trie paths are unique per sorted letter-multiset --
// never revisits the same multiset twice, so there's no analog needed of the old
// Same(&left,&leftPrevious) duplicate check.

void SearchTrieNode( CAnagramSet * set, int totalLen, int anagramWords, CTrieNode * node, int * remaining, int depth, int maxLeftLen )
{
    if ( depth > 0 && NULL != node->words )
    {
        int rightlen = totalLen - depth;

        if ( ! ( ( 2 == anagramWords ) && ( rightlen > g_LongestWord ) ) )
            ProcessLeftMatch( set, totalLen, anagramWords, node->words, depth, remaining );
    }

    if ( depth >= maxLeftLen )
        return;

    for ( int c = 0; c < 26; c++ )
    {
        if ( remaining[ c ] > 0 && NULL != node->children[ c ] )
        {
            remaining[ c ]--;
            SearchTrieNode( set, totalLen, anagramWords, node->children[ c ], remaining, depth + 1, maxLeftLen );
            remaining[ c ]++;
        }
    }
} //SearchTrieNode

// A parallelizable unit of work: a trie node reached at `depth` letters in, with the
// letter budget still remaining at that point. SearchTrieNode picks up from exactly here
// -- it still checks for a match at `node` itself and recurses deeper, since the
// collection walk below only checks ancestors *above* a unit's own depth.

struct TrieWorkUnit
{
    CTrieNode * node;
    int remaining[ 26 ];
    int depth;
};

// Splitting only at the trie's root (26, realistically far fewer, top-level letters) is
// too coarse: some letters' subtrees are vastly more expensive than others (a real,
// measured case had one of 12 populated branches take 93% of the total time), and once
// dynamic scheduling has handed out all the top-level units, an idle thread has nothing
// left to steal -- the one expensive branch just runs alone on its thread while every
// other thread sits idle. Splitting several trie levels deeper instead produces many more,
// smaller units, so a big branch gets broken into pieces that dynamic scheduling can
// spread across idle threads instead of being stuck as one monolithic task.
//
// Matches at depths above splitDepth are checked here, exactly once each, while walking
// down to collect the frontier; SearchTrieNode checks the unit's own node (and deeper)
// when the unit is later processed, so nothing is checked twice.

void CollectWorkUnits( CAnagramSet * set, int totalLen, int anagramWords, CTrieNode * node, int * remaining, int depth, int maxLeftLen, int splitDepth, vector<TrieWorkUnit> & units )
{
    if ( depth >= splitDepth || depth >= maxLeftLen )
    {
        TrieWorkUnit u;
        u.node = node;
        memcpy( u.remaining, remaining, sizeof( u.remaining ) );
        u.depth = depth;
        units.push_back( u );
        return;
    }

    if ( depth > 0 && NULL != node->words )
    {
        int rightlen = totalLen - depth;

        if ( ! ( ( 2 == anagramWords ) && ( rightlen > g_LongestWord ) ) )
            ProcessLeftMatch( set, totalLen, anagramWords, node->words, depth, remaining );
    }

    for ( int c = 0; c < 26; c++ )
    {
        if ( remaining[ c ] > 0 && NULL != node->children[ c ] )
        {
            remaining[ c ]--;
            CollectWorkUnits( set, totalLen, anagramWords, node->children[ c ], remaining, depth + 1, maxLeftLen, splitDepth, units );
            remaining[ c ]++;
        }
    }
} //CollectWorkUnits

void FindPhraseAnagramsTrie( CAnagramSet * set, int * remaining, int totalLen, int anagramWords, bool allowParallel )
{
    int maxLeftLen = totalLen / 2;
    if ( 0 == maxLeftLen )
        return;

    // 3 levels deep is a modest default: enough to turn a handful of lopsided top-level
    // branches into (typically) dozens to low hundreds of smaller units for any input
    // rich enough for load imbalance to matter, without the collection walk itself (which
    // is sequential) becoming a meaningful cost -- it only walks real trie prefixes, never
    // the full combinatorial space.

    const int splitDepth = 2;

    vector<TrieWorkUnit> units;
    CollectWorkUnits( set, totalLen, anagramWords, g_Trie->Root(), remaining, 0, maxLeftLen, splitDepth, units );

    size_t unitCount = units.size();

    // allowParallel is only true for the outermost (non-recursive) call -- see the
    // comment at the recursive call site in ProcessLeftMatch for why attempting to
    // parallelize a nested call would be pure overhead.

    bool runParallel = ( !g_OneThread ) && allowParallel && ( unitCount > 1 );

#ifdef USE_PPL

    if ( !runParallel )
    {
        for ( size_t u = 0; u < unitCount; u++ )
            SearchTrieNode( set, totalLen, anagramWords, units[ u ].node, units[ u ].remaining, units[ u ].depth, maxLeftLen );
    }
    else
    {
        parallel_for( (size_t) 0, unitCount, [&] ( size_t u )
        {
            SearchTrieNode( set, totalLen, anagramWords, units[ u ].node, units[ u ].remaining, units[ u ].depth, maxLeftLen );
        } );
    }

#else // USE_PPL

    if ( !runParallel )
    {
        for ( size_t u = 0; u < unitCount; u++ )
            SearchTrieNode( set, totalLen, anagramWords, units[ u ].node, units[ u ].remaining, units[ u ].depth, maxLeftLen );
    }
    else
    {
        #pragma omp parallel for schedule(nonmonotonic:dynamic)
        for ( long u = 0; u < (long) unitCount; u++ )
            SearchTrieNode( set, totalLen, anagramWords, units[ u ].node, units[ u ].remaining, units[ u ].depth, maxLeftLen );
    }

#endif // USE_PPL
} //FindPhraseAnagramsTrie

void Usage()
{
    printf( "Usage: an [-c] [-i] [-o] [-p] [-t] [-w:X] [word1] [word2] [...]\n" );
    printf( "  Anagram\n" );
    printf( "  arguments: -c    Show the total count of anagrams at the end\n" );
    //printf( "             -f    Use file words.txt (in the an.exe folder) instead of embedded dictionary\n" );
    printf( "             -i    Show Incremental progress as anagrams are found. They won't be sorted and -p won't work.\n" );
    printf( "             -o    One thread only. Default is as parallel as possible.\n" );
    printf( "             -p    Produce all permutations of words in anagrams.\n" );
    printf( "             -t    Show time spent running the app.\n" );
    printf( "             -w:X  X is the maximum number of words in the anagram. Default 3. 1 to 9 are valid.\n" );
    printf( "  examples:  an laying           Find anagrams of the word\n" );
    printf( "             an -w:1 laying      Find single-word anagrams\n" );
    printf( "             an -w:3 playing     Find one to three word anagrams\n" );
    printf( "             an -w:3 -p playing  Find one to three word anagrams and show all permutations\n" );
    printf( "             an phoebe bridgers  Find anagrams\n" );
    printf( "             an -i verylongstringwithmanywords -w:9  -- Show incremental progress because it'll take forever.\n" );
    printf( "  notes:     Combinatorics can still blow up for longer inputs and/or higher -w values\n" );
    printf( "             used together. Use -i to see early results if a search is taking a while.\n" );
    printf( "             The file words.txt must be in the same directory as this .exe. Feel free to add/delete words.\n" );

    exit( 1 );
} //Usage

int compare( const void *a, const void *b )
{
    char ca = * (char *) a;
    char cb = * (char *) b;

    return ca - cb;
} //compare

// This just finds single words that contain all the same letters as the input

void FindAnagrams( CAnString * sortedInput, char * input, CAnagramSet * results )
{
    CWordEntry * list;

    if ( g_SortedWords->TryGetValue( sortedInput, &list ) )
    {
        CWordEntry * l = list;

        do
        {
            // don't add the input word

            if ( strcmp( input, l->word ) )
                AddAnagram( results, l->word, sortedInput->len );

            l = l->next;
        } while ( NULL != l );
    }
} //FindAnagrams

// PermuteWords find permutations of words that constitute anagrams; it's not used in generating
// the set of anagrams unless the user specifies -p

void PermuteWords( CStringBuilder & sb, char ** strings, bool * used, int count, int level, CAnagramSet & perms )
{
    if ( level == count )
    {
        int lenPerm = 0;
        char * pPerm = sb.ToString( lenPerm );
        perms.Add( pPerm, lenPerm );
        return;
    }

    int len = sb.Length();

    for ( int i = 0; i < count; i++ )
    {
        if ( used[ i ] )
            continue;

        used[ i ] = true;

        if ( 0 != len )
            sb.Append( ' ' );

        sb.Append( strings[ i ] );

        PermuteWords( sb, strings, used, count, level + 1, perms );

        sb.SetLength( len );
        used[ i ] = false;
    }
} //PermuteWords

void FindWordsInSentencePermutations( CAnString * anagram, CAnagramSet & permSet )
{
    int cWords = 1 + SpaceCount( anagram );

    unique_ptr<char * []> strings( new char * [ cWords ] );
    unique_ptr<bool []> used( new bool[ cWords ] );

    for ( int i = 0; i < cWords; i++ )
    {
        strings.get()[ i ] = NULL;
        used.get()[ i ] = false;
    }

    // Copy the string and insert null-termination where each space was.
    // Writing through a pointer obtained by casting away const from c_str() is UB even on
    // a non-const string; &theCopy[0] is the standard-legal way to get a writable pointer
    // into a std::string's buffer (guaranteed contiguous since C++11).

    std::string theCopy( anagram->buf );
    char * pTheCopy = &theCopy[ 0 ];

    strings.get()[ 0 ] = pTheCopy;
    int word = 1;
    for ( char *p = pTheCopy; *p; p++ )
    {
        if ( ' ' == *p )
        {
            *p = 0;
            strings.get()[ word++ ] = p + 1;
        }
    }

    CStringBuilder sb;
    PermuteWords( sb, strings.get(), used.get(), cWords, 0, permSet );

    for ( int i = 0; i < cWords; i++ )
        strings.get()[ i ] = NULL;
} //FindWordsInSentencePermutations

void PrintNumberWithCommas( long long n )
{
    if ( n < 0 )
    {
        printf( "-" );
        PrintNumberWithCommas( -n );
        return;
    }
   
    if ( n < 1000 )
    {
        printf( "%lld", n );
        return;
    }

    PrintNumberWithCommas( n / 1000 );
    printf( ",%03lld", n % 1000 );
} //PrintNumberWithCommas

long portable_filelen( FILE * fp )
{
    long current = ftell( fp );
    fseek( fp, 0, SEEK_END );
    long len = ftell( fp );
    fseek( fp, current, SEEK_SET );
    return len;
} //portable_filelen

bool isvalidchar( char c )
{
    return ( ( c <= 'z' ) && ( c >= 'a' ) );
} //isvalidchar

bool ValidateCharsSorted( char * p, int len )
{
    if ( len < 2 )
        return true;

    for ( int x = 0; x < len - 1; x++ )
        if ( p[x] > p[ x + 1 ] )
            return false;

    return true;
} //ValidateCharsSorted

// Insertion sort is faster than qsort for small arrays

void SortChars( char * p, int len )
{
    for ( int i = 1; i < len; i++ )
    {
        int j = i;

        do 
        {
            if ( p[ j - 1 ] < p[ j ] )
                break;

            char t = p[ j ];
            p[ j ] = p[ j - 1 ];
            p[ j - 1 ] = t;
            j--;
        } while ( j > 0 );
    }

    assert( ValidateCharsSorted( p, len ) );
} //SortChars

extern "C" int main( int argc, char * argv[] )
{
    high_resolution_clock::time_point tStart = high_resolution_clock::now();

    unique_ptr<char []> acDictionary( new char[ PATH_MAX ] );
    char * pResult = realpath( argv[ 0 ], acDictionary.get() );

    if ( NULL != pResult )
    {
        char * pLastSlash = strrchr( acDictionary.get(), '\\' );
        if ( NULL == pLastSlash )
            pLastSlash = strrchr( acDictionary.get(), '/' );

        if ( NULL != pLastSlash )
            strcpy( pLastSlash + 1, "words.txt" );
    }
    else
    {
        strcpy( acDictionary.get(), "words.txt" );
    }

    char input[ MaxAnStringLen + 1 ] = { 0 };
    bool showCount = false;
    bool showTime = false;
    bool allPermutations = false;
    g_IncrementalProgress = false;
    int maxAnagramWords = 3;

    for ( int i = 1; i < argc; i++ )
    {
        char * parg = argv[ i ];
        size_t arglen = strlen( parg );
        char c0 = parg[ 0 ];
        char c1 = (char) tolower( parg[ 1 ] );

        if ( '-' == c0 || '/' == c0 )
        {
            if ( 'w' == c1 )
            {
                if ( ':' != parg[2] || arglen < 4 )
                    Usage();

                maxAnagramWords = atoi( parg + 3 );
                if ( maxAnagramWords < 1 || maxAnagramWords > 9 )
                    Usage();
            }
            else if ( 'c' == c1 )
                showCount = true;
            else if ( 'i' == c1 )
                g_IncrementalProgress = true;
            else if ( 'o' == c1 )
                g_OneThread = true;
            else if ( 'p' == c1 )
                allPermutations = true;
            else if ( 't' == c1 )
                showTime = true;
            else
                Usage();
        }
        else
        {
            size_t l = strlen( argv[ i ] );
            size_t inputlen = strlen( input );

            // strcpy below writes l characters plus a NUL terminator starting at offset
            // inputlen, so the last byte written is at offset inputlen + l; that must stay
            // <= the last valid index (_countof(input) - 1), i.e. the combined length must
            // be < _countof(input), not merely <= it (that off-by-one let a maximal-length
            // combined argument overflow input[] by one byte).

            if ( ( l + inputlen ) >= _countof( input ) )
            {
                printf( "input string is too long; only %d characters supported\n", MaxAnStringLen );
                Usage();
            }

            strcpy( input + inputlen, argv[ i ] );
        }
    }

    if ( 0 == input[ 0 ] )
    {
        printf( "no input string specified\n" );
        Usage();
    }

    if ( strlen( input ) + maxAnagramWords > MaxAnStringLen )
    {
        printf( "input string is too long\n" );
        Usage();
    }

    try
    {
        strlwr( input );
        size_t inputLen = strlen( input );
        int uniqueWords = 0;

        g_SortedWords = new CRealDictionary();
        g_Trie = new CTrie();

        const int maxDictionaryLine = MaxAnStringLen;
        char line[ maxDictionaryLine ];
        int collisions = 0;

        CFile fileDictionary( fopen( acDictionary.get(), "rb" ) );
        if ( NULL == fileDictionary.fp )
        {
            printf( "can't open file %s\n", acDictionary.get() );
            return 0;
        }

        // open the dictionary, read it all into RAM, lowercase it (because someone might add a word), and parse it.

        long filelen = portable_filelen( fileDictionary.fp );
        unique_ptr<char []> dictionary( new char [ filelen + 1 ] );
        size_t lread = fread( dictionary.get(), filelen, 1, fileDictionary.fp );
        if ( 1 != lread )
        {
            printf( "unable to read dictionary file\n" );
            return 0;
        }

        fileDictionary.Close();
        dictionary.get()[ filelen ] = 0;
        strlwr( dictionary.get() );

        char * pbuf = dictionary.get();
        char * pbeyond = pbuf + filelen;
        const int MaxDictionaryLineLen = _countof( line ) - 1;

        while ( pbuf < pbeyond )
        {
            int len = 0;
            while ( ( pbuf < pbeyond ) && ( isvalidchar( *pbuf ) ) && ( len < MaxDictionaryLineLen ) )
                line[ len++ ] = *pbuf++;

            // A word that doesn't fit in line[] would otherwise get silently split into
            // bogus fragment "words" below, since the next loop only skips non-letters --
            // if we're still sitting on a letter, the word was truncated. Consume the rest
            // of it and discard the whole thing instead of treating the remainder as a
            // separate word.

            bool overlong = ( pbuf < pbeyond ) && isvalidchar( *pbuf );
            while ( ( pbuf < pbeyond ) && isvalidchar( *pbuf ) )
                pbuf++;

            while ( ( pbuf < pbeyond ) && !isvalidchar( *pbuf ) )
                pbuf++;

            if ( overlong )
                continue;

            if ( 0 != len )
            {
                line[ len ] = 0;

                if ( ( ( maxAnagramWords > 1 ) && ( len <= inputLen ) ) ||
                     ( ( maxAnagramWords == 1 ) && ( len == inputLen ) ) )
                {
                    if ( len > g_LongestWord )
                        g_LongestWord = len;

                    uniqueWords++;                    
                    CAnString wordSorted( line, len );

                    // SortChars runs in about 16ms and qsort in 19ms

                    SortChars( wordSorted.buf, len );
                    // qsort( wordSorted.buf, len, 1, compare );

                    CWordEntry * newEntry = new CWordEntry( line, len );
                    CWordEntry * p = NULL;
                    int entry = g_SortedWords->GetEntry( &wordSorted );

                    if ( g_SortedWords->TryGetValue( & wordSorted, & p, entry ) )
                    {
                        while ( NULL != p->next )
                            p = p->next;

                        p->next = newEntry;
                        collisions++;
                    }
                    else
                    {
                        g_SortedWords->Add( & wordSorted, newEntry, entry );

                        // Only the first occurrence of a given signature needs a trie
                        // insertion -- collisions above append to the SAME CWordEntry
                        // list this node already points to the head of, so the trie
                        // automatically sees later same-signature words too.

                        g_Trie->Insert( & wordSorted, newEntry );
                    }
                }
            }
        }

        dictionary.reset();

        high_resolution_clock::time_point tAfterReadWords = high_resolution_clock::now();

        //printf( "uniquewords: %d, collisions: %d\n", uniqueWords, collisions );

        g_Results = new CAnagramSet();

        CAnString startingInput( input );
        SortChars( startingInput.buf, (int) inputLen );
        //qsort( startingInput.buf, inputLen, 1, compare );

        // Find single-word anagrams

        FindAnagrams( &startingInput, input, g_Results );

        // Find multi-word anagrams

        int startingRemaining[ 26 ] = { 0 };
        for ( int i = 0; i < startingInput.len; i++ )
            startingRemaining[ startingInput.buf[ i ] - 'a' ]++;

        FindPhraseAnagramsTrie( g_Results, startingRemaining, startingInput.len, maxAnagramWords );

        int cAnagrams = g_Results->Count();

        high_resolution_clock::time_point tAfterGenerate = high_resolution_clock::now();

        if ( !g_IncrementalProgress )
        {
            unique_ptr<CAnString * []> array ( new CAnString * [ cAnagrams ] );
            g_Results->GetAnagrams( array.get(), cAnagrams );
            // std::sort with a lambda lets the comparison inline directly into the sort
            // (qsort's function-pointer comparator can't be); same reasoning that already
            // led to replacing qsort with SortChars above, just applied to the one
            // remaining qsort call, which runs over the full result set.
            std::sort( array.get(), array.get() + cAnagrams,
                       []( CAnString * a, CAnString * b ) { return strcmp( a->buf, b->buf ) < 0; } );

            CStringBuilder sb( 4096 );
            CAnagramSet permsSet( 1 );

            // Reused across iterations of the loop below instead of a fresh new[]/delete[]
            // per anagram -- only grows (via .reset()) when a bigger buffer is actually
            // needed, same grow-only spirit as CStringBuilder::EnsureSize.

            unique_ptr<CAnString * []> arrayPerms;
            int arrayPermsCapacity = 0;

            for ( int a = 0; a < cAnagrams; a++ )
            {
                if ( allPermutations )
                {
                    permsSet.Clear();
                    FindWordsInSentencePermutations( array.get()[ a ], permsSet );
                    int permsCount = permsSet.Count();

                    if ( permsCount > arrayPermsCapacity )
                    {
                        arrayPerms.reset( new CAnString *[ permsCount ] );
                        arrayPermsCapacity = permsCount;
                    }

                    permsSet.GetAnagrams( arrayPerms.get(), permsCount );
                    bool first = true;

                    for ( int i = 0; i < permsSet.Count(); i++ )
                    {
                        if ( first )
                            first = false;
                        else
                            sb.Append( ' ', 2 );

                        sb.Append( arrayPerms.get()[ i ]->buf );
                        sb.Append( '\n' );
                    }
                }
                else
                {
                    sb.Append( array.get()[ a ]->buf );
                    sb.Append( '\n' );
                }
            }

            int len = 0;
            printf( "%s", sb.ToString( len ) );
        }

        if ( showCount )
        {
            printf( "  found " );
            PrintNumberWithCommas( cAnagrams );
            printf( " anagram%s for %s\n", ( 1 == cAnagrams ) ? "" : "s", input );
        }

        if ( showTime )
        {
            high_resolution_clock::time_point tAfterRender = high_resolution_clock::now();

            long long totalTime = duration_cast<std::chrono::milliseconds>( tAfterRender - tStart ).count();
            long long dictionaryTime = duration_cast<std::chrono::milliseconds>( tAfterReadWords - tStart ).count();
            long long generateTime = duration_cast<std::chrono::milliseconds>( tAfterGenerate - tAfterReadWords ).count();
            long long renderTime = duration_cast<std::chrono::milliseconds>( tAfterRender - tAfterGenerate ).count();

            printf( "  " ); PrintNumberWithCommas( totalTime); printf( " milliseconds total" );
            printf( " including "); PrintNumberWithCommas( dictionaryTime ); printf( " loading+creating dictionary," );
            printf( " " ); PrintNumberWithCommas( generateTime ); printf( " generating," );
            printf( " " ); PrintNumberWithCommas( renderTime ); printf( " printing\n" );
        }

#if false
        int longestChain = 0;
        int unused = 0;
        int moreThan5 = 0;
        int singles = 0;
        g_SortedWords->Stats( longestChain, unused, moreThan5, collisions, singles );
        printf( "longestchain %d, unused %d, moreThan5 %d, collisions %d, singles %d\n", longestChain, unused, moreThan5, collisions, singles );
#endif

        delete g_SortedWords;
    }
    catch( ... )
    {
        printf( "caught an exception in an.exe, exiting\n" );
    }

    return 0;
} //main


