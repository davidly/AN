# AN
### Anagram generator

Command-line app that generates anagrams of the word(s) specified in the input argument. 

To build on Windows:
    
    Using a Visual Studio x64 Native Tools Command Prompt window, run m.bat

To build on Linux/Mac:

    ./m.sh
    
Usage: an [-c] [-i] [-o] [-p] [-t] [-w:X] [word1] [word2] [...]

    Anagram
    arguments: -c    Show the total count of anagrams at the end
               -i    Show Incremental progress as anagrams are found. They won't be sorted and -p won't work.
               -o    One thread only. Default is as parallel as possible.
               -p    Produce all permutations of words in anagrams.
               -t    Show time spent running the app.
               -w:X  X is the maximum number of words in the anagram. Default 3. 1 to 9 are valid.
    examples:  an laying           Find anagrams of the word
               an -w:1 laying      Find single-word anagrams
               an -w:3 playing     Find one to three word anagrams
               an -w:3 -p playing  Find one to three word anagrams and show all permutations
               an phoebe bridgers  Find anagrams
               an -i verylongstringwithmanywords -w:9  -- Show incremental progress because it'll take forever.
    notes:     The combinatorics blow up beyond about 22 characters for 3-word anagrams
               and even sooner for more-word anagrams. Use -i to see early results.
               The file words.txt must be in the same directory as this .exe. Feel free to add/delete words.
 
The code in an.cxx is covered under GPL v3.
 
The file words.txt comes originally from WordNet. 
 
    https://wordnet.princeton.edu/license-and-commercial-use
     
Words.txt must reside in the same folder as an.exe. Add additional words as you like.
 
D:\>an phoebe bridgers -w:2

briber sheepdog
