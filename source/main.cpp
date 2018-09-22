/*
 KSolve+ - Puzzle solving program.
 Copyright (C) 2007-2013 K�re Krig and Michael Gottlieb

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

// Main struct and control flow of program, with all includes used in it

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <string.h>


#define PARTIAL_TABLE_CONTAINER map
// #define PARTIAL_TABLE_CONTAINER unordered_map  // doesn't work properly yet
// #define PARTIAL_TABLE_CONTAINER gp_hash_table

#if PARTIAL_TABLE_CONTAINER == map
#define PARTIAL_TABLE_CONTAINER_TYPE std::map<std::vector<long long>, char>
#elif PARTIAL_TABLE_CONTAINER == unordered_map
#include <unordered_map>
namespace std {
  template <>
  struct hash<vector<long long>>
  {
    size_t operator()(const vector<long long>& k) const {
    	long long h=0x5bd1e995;
    	for (int j=k.size()-1; j>=0; --j) 
    		h=0x5bd1e995*(h+k[j]); 
    	return h;
    }
  };
}
#define PARTIAL_TABLE_CONTAINER_TYPE std::unordered_map<std::vector<long long>, char>  // results end up wrong; either hash or equality is still broken
#elif defined(__GLIBCXX__)
#include <ext/pb_ds/assoc_container.hpp>
struct vec_long_long_hash {
	int operator()(const std::vector<long long> x) const { 
		return std::_Hash_bytes((void *)&x[0], x.size()*sizeof(long long), 0x5bd1e995); 
	}
};
#define PARTIAL_TABLE_CONTAINER_TYPE __gnu_pbds::gp_hash_table<std::vector<long long>, char, vec_long_long_hash>
#else
#error Error: gp_hash_table requires G++ and the policy based data structures library
#endif


std::map<std::string, int> setnameLookup ;
std::vector<std::string> setNames ;
int setnameIndex(const std::string &s) {
   std::map<std::string, int>::iterator it = setnameLookup.find(s) ;
   if (it == setnameLookup.end()) {
      setnameLookup[s] = setNames.size() ;
      it = setnameLookup.find(s) ;
      setNames.push_back(s) ;
   }
   return it->second ;
}
std::string setnameFromIndex(int i) {
   return setNames[i] ;
}
long long maxmem = 8000000000LL ;
long long partPsize, partOsize;
std::string nameSuffix = "";
int maxDepthMain=999;
int solutionCountMain=0;
int maxResultsMain=999;
int skipPrune=0;
int verbose = 0 ;

struct ksolve {
	#include "data.h"
	#include "move.h"
	#include "blocks.h"
	#include "checks.h"
	#include "indexing.h"
	#include "pruning.h"
	#include "search.h"
	#include "readdef.h"
	#include "readscramble.h"
	#include "god.h"

	static int ksolveMain(int argc, char *argv[]) {

		srand(time(NULL)); // initialize RNG in case we need it
		partPsize = MAX_PARTIAL_PERMUTATION_TABLE_SIZE;
		partOsize = MAX_PARTIAL_ORIENTATION_TABLE_SIZE;
		while (argc > 3 && argv[1][0] == '-') {
			argc-- ;
			argv++ ;
			switch (argv[0][1]) {
				case 'd': maxDepthMain = atoi(argv[1]) ; argc-- ; argv++ ; break ;
				case 'r': maxResultsMain = atoi(argv[1]) ; argc-- ; argv++ ; break ;
				case 'M': maxmem = 1048576 * atoll(argv[1]) ; argc-- ; argv++ ; break ;
				case 'P': partPsize = partOsize = 1048576 * atoll(argv[1]) ; nameSuffix = (std::string)"_"+argv[1]+"M"; argc-- ; argv++ ; break ;
				case 'p': skipPrune++ ; break;
				case 'v': verbose++ ; break ;
				default: std::cout << "Did not understand argument " << argv[0] << std::endl ;
			}
		}
		if (argc != 3){
			std::cerr << "ksolve+ v1.3a - Linux Port by Matt Stiefel\n";
			std::cerr << "(c) 2007-2013 by Kare Krig and Michael Gottlieb\n";
			std::cerr << "Usage: ksolve [def-file] [scramble-file]\n";
			std::cerr << "See readme for additional help.\n";
			return EXIT_FAILURE;
		}


		std::ifstream definitionStream(argv[1]);
		if (!definitionStream.good()){
			std::cout << "Can't open definition file!\n";
			exit(-1);
		}
		std::ifstream scrambleStream(argv[2]);
		if (argv[2][0] != '!' && !scrambleStream.good()){
			std::cout << "Can't open scramble file!\n";
			exit(-1);
		}

		string defFileName(argv[1]);
		string scrambleFileName(argv[2]);
		return ksolveWrapped(definitionStream, scrambleStream, defFileName, scrambleFileName, true);

	}

	static int ksolveWrapped(std::istream &definitionStream,
													 std::istream &scrambleStream,
													 string defFileName,
													 string scrambleFileName,
													 bool usePruneTable)
	{

		clock_t start;
		start = clock();

		// Load the puzzle rules
		Rules ruleset(definitionStream);
		PieceTypes datasets = ruleset.getDatasets();
		Position solved = ruleset.getSolved();
		MoveList moves = ruleset.getMoves();
		std::set<MovePair> forbidden = ruleset.getForbiddenPairs();
		Position ignore = ruleset.getIgnore();
		std::vector<Block> blocks = ruleset.getBlocks();
		std::cout << "Ruleset loaded.\n";

		// Print all generated moves
		std::cout << "Generated moves: ";
		int i = 0;
		MoveList::iterator moveIter;
		for (moveIter = moves.begin(); moveIter != moves.end(); moveIter++) {
			if (moveIter->first != moveIter->second.parentID) {
				if (i>0) std::cout << ", ";
				i++;
				std::cout << moveIter->second.name;
			}
		}
		std::cout << ".\n";

		// Compute or load the pruning tables
		PruneTable tables;
		if (!skipPrune) {
			tables = getCompletePruneTables(solved, moves, datasets, ignore, defFileName, nameSuffix, usePruneTable);
			std::cout << "Pruning tables loaded.\n";
		} else std::cout << "Pruning tables skipped!\n";

		//datasets = updateDatasets(datasets, tables);
		updateDatasets(datasets, tables);

		// God's Algorithm tables
		std::string godHTM = "!";
		std::string godQTM = "!q";
		if (0==godHTM.compare(scrambleFileName)) {
			std::cout << "Computing God's Algorithm tables (HTM)\n";
			godTable(solved, moves, datasets, forbidden, ignore, blocks, 0);
			std::cout << "Time: " << (clock() - start) / (double)CLOCKS_PER_SEC << "s\n";
			return EXIT_SUCCESS;
		} else if (0==godQTM.compare(scrambleFileName)) {
			std::cout << "Computing God's Algorithm tables (QTM)\n";
			godTable(solved, moves, datasets, forbidden, ignore, blocks, 1);
			std::cout << "Time: " << (clock() - start) / (double)CLOCKS_PER_SEC << "s\n";
			return EXIT_SUCCESS;
		}

		// Load the scramble to be solved
		Scramble states(scrambleStream, solved, moves, datasets, blocks);
		std::cout << "Scrambles loaded.\n";

		ScrambleDef scramble = states.getScramble();

		while(scramble.state.size() != 0){
			int depth = 0;
			string temp_a, temp_b;
			temp_a = " ";

			std::cout << "\nSolving " << scramble.name.c_str() << "\n";

			if (scramble.printState == 1) {
				printPosition(scramble.state);
			}

			// give out a warning if we have some undefined permutations on a bandaged puzzle
			if (blocks.size() != 0) {
				bool hasUndefined = false;
				for (int iter=0; iter<scramble.state.size(); iter++) {
					int setsize = scramble.state[iter].size;
					for (int i = 0; i < setsize; i++) {
						if (scramble.state[iter].permutation[i] == -1) {
							hasUndefined = true;
						}
					}
				}
				if (hasUndefined) {
					std::cout << "Warning: using blocks, but scramble has unknown (?) permutations!\n";
				}
			}

			// get rid of any moves that are zeroed out in moveLimits
			// and set .limited for each move
			MoveList moves2;
			MoveList::iterator iter2;
			for (iter2 = moves.begin(); iter2 != moves.end(); iter2++){
				moves2[iter2->first] = iter2->second;
			}
			processMoveLimits(moves2, scramble.moveLimits);

			std::cout << "Depth 0, time to here " << (clock() - start) / (double)CLOCKS_PER_SEC << "s\n";
			clock_t start2 = clock();


			// The tree-search for the solution(s)
			int usedSlack = 0;
			solutionCountMain=0;
			while(solutionCountMain<maxResultsMain) {
				solutionCountMain=0;
				bool foundSolution = treeSolve(scramble.state, solved, moves, datasets, tables, forbidden, scramble.ignore, blocks, depth, scramble.metric, scramble.moveLimits, temp_a, -1, true);
				if (foundSolution || usedSlack > 0) {
					usedSlack++;
					if (usedSlack > scramble.slack) break;
				}
				depth++;
				if (depth > scramble.max_depth){
					std::cout << "\nMax depth reached, aborting.\n";
					break;
				}
				std::cout << "Depth " << depth << ", time " << (clock() - start2) / (double)CLOCKS_PER_SEC << "s\n";
			}
			std::cout << "\n";

			scramble = states.getScramble();
		}

		std::cout << "Total time: " << (clock() - start) / (double)CLOCKS_PER_SEC << "s\n";

		return EXIT_SUCCESS;
	}
};

int main(int argc, char *argv[]) {
	ksolve::ksolveMain(argc, argv);
}

extern "C" void solve(char* definition, char* state) {
	std::istringstream definitionStream(definition);
	std::istringstream scrambleStream(state);
	ksolve::ksolveWrapped(definitionStream, scrambleStream, "dummy", "dummy", false);
}
