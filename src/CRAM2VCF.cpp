//============================================================================
// Name        : CRAM2VCF.cpp
// Author      : Alexander Dilthey (HHU/UKD, NHGRI-NIH), Evan Biederstedt (NYGC), Nathan Dunn (LBNL), Nancy Hansen (NIH), Aarti Jajoo (Baylor), Jeff Oliver (Arizona), Andrew Olsen (CSHL)
// License     : The MIT License, https://github.com/NCBI-Hackathons/Graph_Genomes_CSHL/blob/master/LICENSE
//============================================================================

#include <iostream>
#include <vector>
#include <map>
#include <assert.h>
#include <string>
#include <fstream>
#include <sstream>
#include <exception>
#include <stdexcept>
#include <set>
#include <tuple>
#include <utility>
#include <algorithm>

using namespace std;

std::vector<std::string> split(std::string input, std::string delimiter);
std::string join(std::vector<std::string> parts, std::string delim);
void eraseNL(std::string& s);
int StrtoI(std::string s);
std::string ItoStr(int i);
unsigned int StrtoUI(std::string s);
std::string removeGaps(std::string in);

class startingHaplotype;
void produceVCF(const std::string referenceSequenceID, const std::string& referenceSequence, const std::map<unsigned int, std::vector<startingHaplotype*>>& alignments_starting_at, std::string outputFn);
void printHaplotypesAroundPosition(const std::string& referenceSequence, const std::map<unsigned int, std::vector<startingHaplotype*>>& alignments_starting_at, int posI);

int max_gap_length = 5000;
int max_running_haplotypes_before_add = 5000;

	
class startingHaplotype
{
public:
	std::string ref;
	std::string query;
	std::string query_name;
	long long aligment_start_pos;
	long long alignment_last_pos;
	
	void print()
	{
		std::cerr << "Alignment data " << query_name << "\n";
		std::cerr << "\t Reference: " << ref << "\n";
		std::cerr << "\t Query    : " << query << "\n";
		std::cerr << "\t Ref_start: " << aligment_start_pos << "\n";
		std::cerr << "\t Ref_stop : " << alignment_last_pos << "\n";
		std::cerr << "\n" << std::flush;
	}
};

int main(int argc, char *argv[]) {
	std::vector<std::string> ARG (argv + 1, argv + argc + !argc);
	std::map<std::string, std::string> arguments;

	// arguments["input"] = "C:\\Users\\diltheyat\\Desktop\\Temp\\chr21";
	// arguments["referenceSequenceID"] = "chr21";

	std::map<std::string, std::map<long long, std::set<std::string>>> expectedAlleles;
	
	for(unsigned int i = 0; i < ARG.size(); i++)
	{
		if((ARG.at(i).length() > 2) && (ARG.at(i).substr(0, 2) == "--"))
		{
			std::string argname = ARG.at(i).substr(2);
			std::string argvalue = ARG.at(i+1);
			arguments[argname] = argvalue;
		}
	}

	assert(arguments.count("input"));
	assert(arguments.count("referenceSequenceID"));

	std::string outputFn = arguments.at("input") + ".VCF";
	std::string doneFn = outputFn + ".done";
	std::ofstream doneStream;
	doneStream.open(doneFn.c_str());
	if(! doneStream.is_open())
	{
		throw std::runtime_error("Cannot open " + doneFn + " for writing!");
	}
	doneStream << 0 << "\n";
	doneStream.close();
	
	
	std::ifstream inputStream;
	inputStream.open(arguments.at("input").c_str());
	if(! inputStream.is_open())
	{
		throw std::runtime_error("Could not open file "+arguments.at("input"));
	}
	assert(inputStream.good());
	std::string referenceSequence;
	std::getline(inputStream, referenceSequence);
	eraseNL(referenceSequence);

	int n_alignments_loaded = 0;
	int n_alignments_split = 0;
	int n_alignments_sub = 0;
	std::map<unsigned int, std::vector<startingHaplotype*>> alignments_starting_at;
	std::string line;

	/* 

       We read in the data produced by the CRAM2VCF script.

       These are basically pairwise sequence alignments between reference and input contigs in a simple text format.

       By definition, at any given reference position, we have to be able to reconstitute a valid multiple sequence alignment of the reference
       and the contigs from the pairwise reference<->contig alignments. One crucial requirement for this is that we sometimes have to encode
       'double-gap' columns in the pairwise alignments that specify gaps for both the sequence and the reference.

       Also the following algorithms become more complex when there are large deletions relative to the reference. Therefore we apply a filter for maximum
       gap region length:

           (max_running_gap_length <= max_gap_length)
       
       This parameter can be played around with!

     */
	   

	while(inputStream.good())
	{
		std::getline(inputStream, line);
		eraseNL(line);
		if(line.length())
		{
			std::vector<std::string> line_fields = split(line, "\t");
			assert(line_fields.size() == 5);
			startingHaplotype* h = new startingHaplotype();
			h->ref = line_fields.at(0);
			h->query = line_fields.at(1);
			h->query_name = line_fields.at(2);
			h->aligment_start_pos = StrtoUI(line_fields.at(3));
			h->alignment_last_pos = StrtoUI(line_fields.at(4))+1;
			
			// determine alleles expected to be found
			{
				long long runningRefC_0based = (h->aligment_start_pos - 1);
				std::string runningRefAllele;
				std::string runningQueryAllele;
				
				for(unsigned int i = 0; i < h->ref.length(); i++)
				{
					unsigned char c_ref = h->ref.at(i);
					unsigned char c_query = h->query.at(i);

					if((c_ref != '-') && (c_ref != '*'))
					{
						// empty alleles
						if((runningRefAllele.length() == 1) && (runningQueryAllele.length() == 1) && (runningRefAllele != runningQueryAllele))
						{
							if((runningRefAllele != "-") && (runningRefAllele != "*") && (runningQueryAllele != "-") && (runningQueryAllele != "*"))
							{
								expectedAlleles[arguments.at("referenceSequenceID")][runningRefC_0based].insert(runningQueryAllele);
							}
						}
						runningRefAllele = "";
						runningQueryAllele = "";
					}
					
					if((c_ref != '-') && (c_ref != '*'))
					{
						runningRefC_0based++;
					}
					
					runningRefAllele.push_back(c_ref);
					runningQueryAllele.push_back(c_query);
				}	
			}
			
			// this is a hack - if this is ever violated, carry out proper scan for the first match in the alignment
			if(h->aligment_start_pos == 0)
			{
				assert(h->ref.at(1) != '-');
				assert(h->query.at(1) != '-');
				h->aligment_start_pos = 1;
				h->ref = h->ref.substr(1);
				h->query = h->query.substr(1);
			}
			
			long long lastPos_control = (long long)h->aligment_start_pos - 1;
			long long firstMatchPos_reference = -1;
			long long lastMatchPos_reference = -1;
			
			std::string running_ref;
			std::string running_query;
			
			long long runningNonMatchPositions = 0;
			long long runningRefGapCharacters = 0;
			long long runningQueryGapCharacters = 0;
			long long runningRefPos = (long long)h->aligment_start_pos - 1;
			//long long total_removedGappyRegions = 0;
			std::vector<startingHaplotype*> haplotype_parts;
			
			std::string reconstituted_ref;
			std::string reconstituted_query;
			
			for(unsigned int i = 0; i < h->ref.length(); i++)
			{
				unsigned char c_ref = h->ref.at(i);	
				unsigned char c_query = h->query.at(i);	
				
				if((c_ref != '-') && (c_ref != '*'))
				{
					runningRefPos++;
				}
				
				bool isMatchOrMismatch = ((c_ref != '-') && (c_ref != '*') && (c_query != '-') && (c_query != '*'));
				bool isRefGap = ((c_ref == '-') || (c_ref == '*'));  
				bool isQueryGap = ((c_query == '-') || (c_query == '*'));
				
				if((i == 0) || (i == (h->ref.length() - 1)))
				{
					assert(isMatchOrMismatch);					
				}
			
				if(isMatchOrMismatch)
				{
					if(runningQueryGapCharacters > max_gap_length)
					{
						// we have a match, but too many gaps, so we want to close!
						
						assert(firstMatchPos_reference != -1);
						long long remainingCharacters = running_ref.length() - runningNonMatchPositions;
						assert(remainingCharacters >= 0);
						std::string removeRef;
						std::string removeQuery;
						if(runningNonMatchPositions > 0)
						{
								assert(running_ref.length() > remainingCharacters);
								removeRef = running_ref.substr(remainingCharacters);
								removeQuery = running_query.substr(remainingCharacters);
						}
						running_ref = running_ref.substr(0, remainingCharacters);
						running_query = running_query.substr(0, remainingCharacters);
						assert(running_ref.length() == remainingCharacters);
						assert(running_query.length() == remainingCharacters);
						//total_removedGappyRegions += runningNonMatchPositions;
						
						reconstituted_ref.append(running_ref);
						reconstituted_query.append(running_query);
						
						reconstituted_ref.append(removeRef);
						reconstituted_query.append(removeQuery);

						if(running_ref.length())
						{
							startingHaplotype* h_part = new startingHaplotype();
							h_part->ref = running_ref;
							h_part->query = running_query;
							h_part->query_name = h->query_name + "_part" + std::to_string(haplotype_parts.size());
							h_part->aligment_start_pos = firstMatchPos_reference;
							h_part->alignment_last_pos = lastMatchPos_reference;
							haplotype_parts.push_back(h_part);
							/*
							std::cerr << "New alignment from " << h->query_name << "\n";
							std::cerr << "\tLength: " << running_ref.length() << "\n";
							std::cerr << "\tR Start : " << h_part->aligment_start_pos << "\n";
							std::cerr << "\tR Stop  : " << h_part->alignment_last_pos << "\n";
							std::cerr << "\trunningRefGapCharacters  : " << runningRefGapCharacters << "\n";
							std::cerr << "\trunningNonMatchPositions  : " << runningNonMatchPositions << "\n";
							std::cerr << "\trunningQueryGapCharacters  : " << runningQueryGapCharacters << "\n";
							
							//std::cerr << "\tC Start : " << h_part->aligment_start_pos << "\n";
							//std::cerr << "\tC Stop  : " << h_part->alignment_last_pos << "\n";
							//std::cerr << "\tRef    : " << running_ref << "\n";
							//std::cerr << "\tQuery  : " << running_query << "\n";
							std::cerr << std::flush;
							*/

							assert(!((h_part->aligment_start_pos == 46398487) && (h_part->alignment_last_pos == 46398489)));
						}
						
						running_ref.clear();
						running_query.clear();
						firstMatchPos_reference = -1;
					}		
					
					if(firstMatchPos_reference == -1)
					{
						firstMatchPos_reference = runningRefPos;
					}
					
					lastMatchPos_reference = runningRefPos;
					
					runningNonMatchPositions = 0;
					runningRefGapCharacters = 0;
					runningQueryGapCharacters = 0;
				}
				else
				{
					runningNonMatchPositions++;
					if(isRefGap && !isQueryGap)
						runningRefGapCharacters++;
					if(isQueryGap && !isRefGap)
						runningQueryGapCharacters++;					
				}
				
				running_ref.push_back(c_ref);
				running_query.push_back(c_query);
				
				if((c_ref != '-') and (c_ref != '*'))
				{
					lastPos_control++;
				}
			}
			//std::cerr << "lastPos_control: " << lastPos_control << "\n";
			//std::cerr << "h->alignment_last_pos: " << h->alignment_last_pos << "\n" << std::flush;
			if(lastPos_control != ((long long)h->alignment_last_pos))
			{
				std::cerr << "h->aligment_start_pos: " << h->aligment_start_pos << "\n";
				std::cerr << "lastPos_control: " << lastPos_control << "\n";
				std::cerr << "h->alignment_last_pos: " << h->alignment_last_pos << "\n";
				std::cerr << std::flush;
			}
			assert(lastPos_control == ((long long)h->alignment_last_pos));
			if(lastPos_control != (lastMatchPos_reference))
			{
				std::cerr << "h->aligment_start_pos: " << h->aligment_start_pos << "\n";
				std::cerr << "lastPos_control: " << lastPos_control << "\n";
				std::cerr << "h->alignment_last_pos: " << h->alignment_last_pos << "\n";
				std::cerr << "lastMatchPos_reference: " << lastMatchPos_reference << "\n";
				std::cerr << std::flush;
			}
			assert(lastPos_control == lastMatchPos_reference);
			assert(runningNonMatchPositions <= max_gap_length);

			if(running_ref.length())
			{
				startingHaplotype* h_part = new startingHaplotype();
				h_part->ref = running_ref;
				h_part->query = running_query;
				h_part->query_name = h_part->query_name + "_part" + std::to_string(haplotype_parts.size());
				h_part->aligment_start_pos = firstMatchPos_reference;
				h_part->alignment_last_pos = lastMatchPos_reference;
				reconstituted_ref.append(running_ref);
				reconstituted_query.append(running_query);				
				haplotype_parts.push_back(h_part);
			}
						
			assert(reconstituted_ref == h->ref);
			assert(reconstituted_query == h->query);
									
			if(haplotype_parts.size() > 1)
			{
				/*
				std::cerr << "Split " << h->query_name << " into multiple parts -- removed " << total_removedGappyRegions << "gaps.\n";		
				h->print();
				for(unsigned int pI = 0; pI < haplotype_parts.size(); pI++)
				{
					std::cerr << "Part " << pI << " ";
					haplotype_parts.at(pI)->print();
				}
				assert(1 == 0);
				*/
				n_alignments_split++;
				delete(h);
				for(auto hP : haplotype_parts)
				{
					alignments_starting_at[hP->aligment_start_pos].push_back(hP);
					n_alignments_sub++;
				}
				// std::cerr << "\t\tSubalignments: " << n_alignments_sub << "\n" << std::flush;
			}
			else
			{
				alignments_starting_at[h->aligment_start_pos].push_back(h);
				delete(haplotype_parts.at(0));
				n_alignments_loaded++;					
			}
			

			
			/*
			int running_gap_length = 0;
			int max_running_gap_length = 0;
			std::vector<std::string>
			for(unsigned int i = 0; i < h->ref.length(); i++)
			{
				unsigned char c_ref = h->ref.at(i);
				unsigned char c_q = h->query.at(i);
				if((c_ref == '-') or (c_ref == '*'))
				{
					running_gap_length++;
				}
				else
				{
					if(running_gap_length)
					{
						if(running_gap_length > max_running_gap_length)
							max_running_gap_length = running_gap_length;
						
						if(max_running_gap_length > max_gap_length)
						{
							
						}
					}
					running_gap_length = 0;
				}
			}
			assert(running_gap_length == 0);

			if(max_running_gap_length <= max_gap_length)
			{
				alignments_starting_at[h->aligment_start_pos].push_back(h);
				n_alignments_loaded++;
			}
			else
			{
				
			}
			*/
		}
	}
	std::cout << "For max. gap length " << max_gap_length << "\n";
	std::cout << "\t" << "n_alignments_loaded" << ": " << n_alignments_loaded << "\n";
	std::cout << "\t" << "n_alignments_split" << ": " << n_alignments_split << " (into " << n_alignments_sub << " subalignments.)\n";
	std::cout << std::flush;

	std::string fn_files_SNPs = arguments.at("input")+".VCF.expectedSNPs";
	std::ofstream SNPsstream;
	SNPsstream.open(fn_files_SNPs.c_str());
	assert(SNPsstream.is_open());
	
	produceVCF(arguments.at("referenceSequenceID"), referenceSequence, alignments_starting_at, outputFn);

	for(auto SNPsPerRefID : expectedAlleles)
	{
		for(auto refPos : SNPsPerRefID.second)
		{
			for(auto allele : refPos.second)
			{
				SNPsstream << SNPsPerRefID.first << "\t" << (refPos.first+1) << "\t" << allele << "\n";
			}
		}
	}

	doneStream.open(doneFn.c_str());
	if(! doneStream.is_open())
	{
		throw std::runtime_error("Cannot open " + doneFn + " for writing!");
	}
	doneStream << 1 << "\n";
	doneStream.close();	

	return 0;
}

void produceVCF(const std::string referenceSequenceID, const std::string& referenceSequence, const std::map<unsigned int, std::vector<startingHaplotype*>>& alignments_starting_at, std::string outputFn)
{
	std::ofstream outputStream;
	outputStream.open(outputFn.c_str());
	if(! outputStream.is_open())
	{
		throw std::runtime_error("Cannot open " + outputFn + " for writing!");
	}

	int n_alignments = 0;
	for(auto startPos : alignments_starting_at)
	{
			n_alignments += startPos.second.size();
	}

    // STEP 1: Gap structure
	// first step: count how many gaps we have in the underlying MSA-like structure at each reference position
	// gap_structure.at(i) counts the number of gaps that occur between reference position i - 1 and i (0-based).
	// this needs to be consistent for all input alignments
    // coverage_structure.at(i) is calculated for output/debug purposes.

	std::vector<int> gap_structure;
	std::vector<int> coverage_structure;
	gap_structure.resize(referenceSequence.length(), -1);
	coverage_structure.resize(referenceSequence.length(), 0);
	int examine_gaps_n_alignment = 0;
	for(auto startPos : alignments_starting_at)
	{
		for(startingHaplotype* alignment : startPos.second)
		{
			assert(startPos.first == alignment->aligment_start_pos);

			long long start_pos = (int)startPos.first - 1;
			long long ref_pos = start_pos;
			int running_gaps = 0;

			for(unsigned int i = 0; i < alignment->ref.length(); i++)
			{
				unsigned char c_ref = alignment->ref.at(i);
				if((c_ref == '-') or (c_ref == '*'))
				{
					running_gaps++;
				}
				else
				{
					if(ref_pos != start_pos)
					{
						if(gap_structure.at(ref_pos) == -1)
						{
							gap_structure.at(ref_pos) = running_gaps;
						}
						else
						{
							if(gap_structure.at(ref_pos) != running_gaps)
							{
								std::cerr << "Gap structure mismatch at position " << ref_pos << " - this is alignment " << examine_gaps_n_alignment << " / " << alignment->query_name << ", have existing value " << gap_structure.at(ref_pos) << ", want to set " << running_gaps << "\n" << std::flush;
								std::cerr << "Alignment start " << alignment->aligment_start_pos << "\n";
								std::cerr << "Alignment stop " << alignment->alignment_last_pos << "\n";
								std::cerr << std::flush;
								throw std::runtime_error("Gap structure mismatch");
							}

						}
					}
					
					ref_pos++;
					running_gaps = 0;
					coverage_structure.at(ref_pos)++; 
					
					assert(c_ref == referenceSequence.at(ref_pos));					
				}
			}

			examine_gaps_n_alignment++;
			assert(ref_pos == alignment->alignment_last_pos);
		}
	}

    // STEP 2: Output some stuff
	// printHaplotypesAroundPosition(referenceSequence, alignments_starting_at, 10014331);
	// assert( 3==5 );

	std::cout << "Loaded " << examine_gaps_n_alignment << " alignments.\n";
	
	std::cout << "Coverage structure:\n";
	int coverage_window_length = 10000;
	for(unsigned int pI = 0; pI < coverage_structure.size(); pI += coverage_window_length)
	{
		int coverage_in_window = 0;
		unsigned int last_window_pos = (pI+coverage_window_length) - 1;
		if(last_window_pos > (coverage_structure.size() - 1))
			last_window_pos = coverage_structure.size() - 1;

		for(unsigned int j = pI; j <= last_window_pos; j++)
		{
			assert(j < coverage_structure.size());
			coverage_in_window += coverage_structure.at(j);
		}
		double avg_coverage = (double) coverage_in_window / (double)(last_window_pos - pI + 1);

		if((pI >= 15000000) && (pI <= 17000000))
			std::cout << "\t" << "Window starting at pI = " << pI << " => avg. coverage " << avg_coverage << "\n";
	}
	std::cout << std::flush;

	std::set<const startingHaplotype*> known_haplotype_pointers;
	for(auto startingPos : alignments_starting_at)
	{
		for(startingHaplotype* sH :  startingPos.second)
		{
			known_haplotype_pointers.insert(sH);
		}
	}

	// openHaplotype data structure:
	// (1) running haplotype sequence (string)
    // (2) pointer to input alignment we're copying from - 0 means reference
    // (3) position within the input alignment - the (inclusive) position up to which we've copied stuff already into the first element. (I think this is ignored and can be -1 if (2) == 0, i.e. reference)

	using openHaplotype = std::tuple<std::string, const startingHaplotype*, int>;
	std::vector<openHaplotype> open_haplotypes;

	// we init with an empty running haplotype that copies the reference
	openHaplotype initH = std::make_tuple("", (const startingHaplotype*)0, -1);
	open_haplotypes.push_back(initH);

	int start_open_haplotypes = 0;
	int opened_alignments = 0;

    // STEP 3: Build the graph / VCF
	//
	// We do this in a stepwise fashion, reference position by reference position
	//
	// As we go along the reference, we keep track of possible haplotypes (we say that this is a list of 'running' or 'open' haplotypes)
	// that consist of the input contigs (to be precise, their alignments) and potential recombination events between the input contigs.
	//
	// We recombine promiscuously: whenever a new alignment starts, it can recombine into all existing haplotypes,
	// and whenever it ends, it can recombine back into other running haplotypes.
	//
	// Whenever the VCF format allows us to empty the list of possible haplotypes, we do so (i.e. when there's a position at which all haplotypes are REF); when this happens,
    // we write all open haplotypes as variants into the VCF, and shorten the possible haplotypes to the last position.
	// If any running haplotypes (and their associated data, see the openHaplotype structure) are identical at this point, we combine them.
	// 	
	// !! An important corollary is that, at any point in time, all open haplotypes start at the same reference position.
	// !! This information is stored in the variable start_open_haplotypes
	// !! I.e. everything up to start_open_haplotypes has been processed already / stored in the output VCF.
	//
	// The 'state space' of our graph builder increases as new alignments appear and disappear as we walk along the reference,
    // and it becomes smaller each time we dump some variant positions into VCF. If one of the running alignments represents a
    // long-running gap that hasn't been closed yet, this prevents us from the VCF output stage; therefore big gaps
	// lead to decreased performance and alignments containing them are sometimes filtered (see above).
	//
	// While doing all of this, we need to do some data gymnastics to make sure that the MSA structure of the
    // input alignments is maintained and respected.
	//

	bool modifiedLastPos = false;
	for(int posI = 0; posI < (int)referenceSequence.length(); posI++)
	{

		/*
		if((posI >= 10014327) && (posI <= 10014332))
		{
			std::cout << "Position " << posI << " open positions:\n";
			for(unsigned int hI = 0; hI < open_haplotypes.size(); hI++)
			{
				std::cout << "\tOpen haplotype " << hI << "\n";
				std::cout << "\t\tSequence: " << std::get<0>(open_haplotypes.at(hI)) << "\n";
				std::cout << "\t\tCopying from: " << ((std::get<1>(open_haplotypes.at(hI)) == 0) ? "REF" : std::get<1>(open_haplotypes.at(hI))->query_name) << "\n";
				std::cout << "\t\tPosition: " << std::get<2>(open_haplotypes.at(hI)) << "\n";
				std::cout << std::flush;
			}
			if(posI == 10014332)
			{
				assert(3 == 5);
			}
		}
		*/
		
		// make sure that all open haplotypes really 'extend' up to reference position posI in MSA space
		// therefore: consume (for each open haplotype) all gaps "before" the current reference position
		
		size_t haplotype_length = 0;
		for(const openHaplotype& haplotype : open_haplotypes)
		{
			haplotype_length = std::get<0>(haplotype).length();
			break;
		}			
			
		long long duplicated = -1;

		if(modifiedLastPos)
		{
			duplicated = 0;
			std::set<std::string> open_haplotypes_keys;
			for(const openHaplotype& haplotype : open_haplotypes)
			{
				std::stringstream haplotype_key_str;
				haplotype_key_str << 	std::get<0>(haplotype) << ";" <<
									std::get<1>(haplotype) << ";" <<
									std::get<2>(haplotype);
				std::string haplotype_key = haplotype_key_str.str();
				if(open_haplotypes_keys.count(haplotype_key) != 0)
				{
					duplicated++;
				}
				open_haplotypes_keys.insert(haplotype_key);
			}			
			
			if(duplicated)
			{
				std::vector<openHaplotype> new_open_haplotypes;
				open_haplotypes_keys.clear();
				for(const openHaplotype& haplotype : open_haplotypes)
				{
					std::stringstream haplotype_key_str;
					haplotype_key_str << 	std::get<0>(haplotype) << ";" <<
										std::get<1>(haplotype) << ";" <<
										std::get<2>(haplotype);
					std::string haplotype_key = haplotype_key_str.str();
					if(open_haplotypes_keys.count(haplotype_key) == 0)
					{
						new_open_haplotypes.push_back(haplotype);
					}
					open_haplotypes_keys.insert(haplotype_key);
				}	
				open_haplotypes = new_open_haplotypes;
				std::cout << "\tRemoved " << duplicated << " haplotypes.\n" << std::flush;
			}
			
			modifiedLastPos = false;
		}
		
		if(((posI % 1000) == 0) or (0 && open_haplotypes.size() > 100))
		{
			std::cout << posI << ", open haplotypes: " << open_haplotypes.size() << " -- duplicated: " << duplicated << " -- length: " << haplotype_length << "\n";
		}
		
		for(openHaplotype& haplotype : open_haplotypes)
		{
			if(std::get<1>(haplotype) != 0)
			{
				if(std::get<2>(haplotype) == ((int)std::get<1>(haplotype)->ref.length() - 1)) // if we're at the end of the alignment already, we may need to copy in gaps, as final gaps wouldn't be part of the alignment
				{
					int n_gaps = gap_structure.at(posI-1);
					if(n_gaps == -1)
						n_gaps = 0;

					std::string gaps;
					gaps.resize(n_gaps, '-');
					assert((int)gaps.length() == n_gaps);
					std::get<0>(haplotype) += gaps;
				}
				else
				{
					if(((std::get<1>(haplotype)->ref.at(std::get<2>(haplotype))) == '-') || (std::get<1>(haplotype)->ref.at(std::get<2>(haplotype)) == '*'))
					{
						// not sure what this is to tell us
						std::cerr << "Position " << std::get<2>(haplotype) << " is gap in one of our haplotypes!";
					}

					
					int nextPos = std::get<2>(haplotype)+1;
					std::string additionalExtension;
					while((nextPos < (int)std::get<1>(haplotype)->ref.length()) && ((std::get<1>(haplotype)->ref.at(nextPos) == '-') || (std::get<1>(haplotype)->ref.at(nextPos) == '*')))
					{
						additionalExtension += std::get<1>(haplotype)->query.substr(nextPos, 1);
						nextPos++;
					}
					int consumedUntil = nextPos - 1;

					std::get<0>(haplotype).append(additionalExtension);
					std::get<2>(haplotype) = consumedUntil;
				}
			}
			else // even if we're copying from the reference, we might have to put in some gaps if this is required by the MSA
			{
				if(posI > 0)
				{

					int n_gaps = gap_structure.at(posI-1);
					if(n_gaps == -1)
						n_gaps = 0;

					std::string gaps;
					gaps.resize(n_gaps, '-');
					assert((int)gaps.length() == n_gaps);
					std::get<0>(haplotype) += gaps;
				}
			}
		}

		// check that all open haplotypes - i.e. up to the current reference position - have the same length
		// this is required because we're dealing with an MSA-like structure here, and we want all open
		// haplotypes to have reached the same 'column' in the MSA
		int assembled_h_length = -1;
		for(openHaplotype haplotype : open_haplotypes)
		{
			if(assembled_h_length == -1)
			{
				assembled_h_length = std::get<0>(haplotype).length();
			}

			if(assembled_h_length != (int)std::get<0>(haplotype).length())
			{
				std::cerr << "Initial II length mismatch " << posI << " " << assembled_h_length << "\n"; // [@gap_structure[(posI-3) .. (posI+1)]]
				for(openHaplotype oH2 : open_haplotypes)
				{
					std::cerr << "\t" << std::get<0>(oH2).length() << "\tconsumed until: " << std::get<2>(oH2) << ", of length " << ((std::get<1>(oH2) == 0) ? "REF" : ("nonRef " + std::get<1>(oH2)->query_name + " / length " + ItoStr(std::get<1>(oH2)->ref.length()))) << "\n";
				}
				printHaplotypesAroundPosition(referenceSequence, alignments_starting_at, posI);
				assert(2 == 4);
			}
		}

		// it might be that we have additional alignments starting at posI
		// if so, we make a list of these to be integrated into the openHaplotypes set	
		// when we integrate a new alignment, we assume that the new alignment can 'recombine'
		// into each of the open haplotypes.
		unsigned char refC = referenceSequence.at(posI);
		std::vector<const startingHaplotype*> new_haplotypes;
		if(alignments_starting_at.count(posI))
		{
			for(startingHaplotype* sH :  alignments_starting_at.at(posI))
			{
				assert(known_haplotype_pointers.count(sH));
				new_haplotypes.push_back(sH);
			}
		}

		// the following block represents the 'recombining into' step ...
		unsigned int open_haplotypes_size = open_haplotypes.size();
		for(const startingHaplotype* new_haplotype : new_haplotypes)
		{
			if(open_haplotypes.size() <= max_running_haplotypes_before_add)
			{			
				if(open_haplotypes_size > 0) // not quite sure why this should ever be < 1, but might be condition reached towards the end of a chromosome
				{
					opened_alignments++;

					for(int existingHaploI = 0; existingHaploI < (int)open_haplotypes_size; existingHaploI++)
					{
						// ... we take the sequence of an existing open haplotype, but stipulate that from now onwards we copy from the new alignment (new_haplotype)
						openHaplotype new_haplotype_copy_this = std::make_tuple(std::get<0>(open_haplotypes.at(existingHaploI)), new_haplotype, -1);
						open_haplotypes.push_back(new_haplotype_copy_this);
						modifiedLastPos = true;
					}

					// in addition to recombining into an existing variant haplotype, we can also
					// recombine into the reference - which we need to copy from position start_open_haplotypes onwards.
					int open_span = posI - start_open_haplotypes;
					int start_reference_extraction = start_open_haplotypes;
					int stop_reference_extraction = posI - 1;
					if(!(stop_reference_extraction >= start_reference_extraction))
					{
						std::cerr << "stop_reference_extraction" << ": " << stop_reference_extraction << "\n";
						std::cerr << "start_reference_extraction" << ": " << start_reference_extraction << "\n";
						std::cerr << std::flush;
					}
					assert(stop_reference_extraction >= start_reference_extraction); // die Dumper("Weird", start_reference_extraction, stop_reference_extraction) unless(stop_reference_extraction >= start_reference_extraction);
					std::string referenceExtraction;
					referenceExtraction.reserve(stop_reference_extraction - start_reference_extraction + 1);
					for(int refI = start_reference_extraction; refI <= stop_reference_extraction; refI++)
					{
						referenceExtraction.push_back(referenceSequence.at(refI));

						int n_gaps = gap_structure.at(refI);
						if(n_gaps == -1)
							n_gaps = 0;
						std::string gaps;
						gaps.resize(n_gaps, '-');
						assert((int)gaps.length() == n_gaps);
						referenceExtraction.append(gaps);
					}

					//new_haplotype_referenceSequence = [substr(referenceSequence, start_open_haplotypes, open_span), new_haplotype, -1];
					openHaplotype new_haplotype_referenceSequence = std::make_tuple(referenceExtraction, new_haplotype, -1);

					//missing = assembled_h_length - open_span;
					//die Dumper(posI, start_open_haplotypes, missing, open_span, assembled_h_length) unless(missing >= 0);
					//missingStr = '*' x missing;
					//die unless(length(missingStr) == missing);
					//new_haplotype_referenceSequence->[0] .= missingStr;

					open_haplotypes.push_back(new_haplotype_referenceSequence);

					std::cout << "Position " << posI << ", enter new haplotype " << new_haplotype->query_name << " --> " << open_haplotypes.size() << " haplotypes.\n" << std::flush;

				}
			}
			else
			{
				std::cout  << "Position " << posI << ", would have new haplotype " << new_haplotype->query_name << ", but have " << open_haplotypes_size << " open pairs already, so skip.\n" << std::flush;
			}				
		}
		

		// some debug information
		if(posI == 7652900)
		{
			std::cerr << "Pre-exit haplotype lengths " << posI << "\n"; // [@gap_structure[(posI-3) .. (posI+1)]]
			for(openHaplotype oH2 : open_haplotypes)
			{
				std::cerr << "\t" << std::get<0>(oH2).length() << "\tconsumed until: " << std::get<2>(oH2) << ", of length " << ((std::get<1>(oH2) == 0) ? "REF" : ("nonRef " + std::get<1>(oH2)->query_name + " / length " + ItoStr(std::get<1>(oH2)->ref.length()))) << "\n";
			}
		}

		// whenever we've exhausted an input alignment, we recombine back into all other running haplotypes
		// that is, we switch the template alignment for these running haplotypes to ref / another, non-exhausted running haplotype (all options)
		// 
		// NB: This step doesn't remove any elements from open_haplotypes - on the contrary, it can add elements (by recombination into other open haplotypes)
		//

		std::set<std::string> inner_open_haplotypes_keys;

		open_haplotypes_size = open_haplotypes.size();
		std::set<unsigned int> exitedHaplotype;
		for(unsigned int outer_haplotype_I = 0; outer_haplotype_I < open_haplotypes_size; outer_haplotype_I++)
		{
			openHaplotype& haplotype = open_haplotypes.at(outer_haplotype_I);

			if(std::get<1>(haplotype) != 0) // i.e. non-ref
			{
				if(std::get<2>(haplotype) == ((int)std::get<1>(haplotype)->ref.length() - 1)) // i.e. we're done with this input alignment
				{
					if(inner_open_haplotypes_keys.size() == 0)
					{
						for(const openHaplotype& haplotype : open_haplotypes)
						{
							std::stringstream haplotype_key_str;
							haplotype_key_str << 	std::get<0>(haplotype) << ";" <<
												std::get<1>(haplotype) << ";" <<
												std::get<2>(haplotype);
							std::string haplotype_key = haplotype_key_str.str();
							inner_open_haplotypes_keys.insert(haplotype_key);
						}
					}
					
					std::cerr << "Position " << posI << ", exit haplotype " << std::get<1>(haplotype)->query_name << " length " << std::get<0>(haplotype).length() << " (open haplotypes " << open_haplotypes.size() << ")\n" << std::flush;
					// print "exit one\n";

					// recombine into the reference
					std::get<1>(haplotype) = 0;
					std::get<2>(haplotype) = -1;
					exitedHaplotype.insert(outer_haplotype_I);

					size_t expected_haplotype_length = std::get<0>(haplotype).length();
					std::cerr << "\texpected_haplotype_length: " << expected_haplotype_length << "\n";
					modifiedLastPos = true;
					
					if(open_haplotypes.size() <= max_running_haplotypes_before_add)
					{  
				
						for(unsigned int existingHaploI = 0; existingHaploI < (int)open_haplotypes_size; existingHaploI++)
						{
							openHaplotype& haplotype = open_haplotypes.at(outer_haplotype_I);
							
							if(posI > 7650000)
							{
								//std::cerr << "Outer haplotype " << outer_haplotype_I << " / inner haplotype " << existingHaploI << ": length of " << (&haplotype) << " " << std::get<0>(haplotype).length() << "\n" << std::flush;
							}
							//if(existingHaploI == existingHaploI) // this looks like a bug - nonsensical -- might be instead: existingHaploI == outer_haplotype_I
							if(existingHaploI == outer_haplotype_I)
							{
								continue;
							}

							if(exitedHaplotype.count(existingHaploI))
								continue;

							// create and add a new recombination haplotype
							if(std::get<0>(haplotype).length() != expected_haplotype_length)
							{
								std::cerr << "std::get<0>(haplotype).length() is " << std::get<0>(haplotype).length() << "\n" << std::flush;
							}
							assert(std::get<0>(haplotype).length() == expected_haplotype_length);
							openHaplotype new_haplotype_copy_this = std::make_tuple(std::string(std::get<0>(haplotype)), std::get<1>(open_haplotypes.at(existingHaploI)), std::get<2>(open_haplotypes.at(existingHaploI)));
							assert(std::get<0>(haplotype).length() == expected_haplotype_length);
							assert(std::get<0>(new_haplotype_copy_this).length() == expected_haplotype_length);

							// ... and of course the new haplotype must not be exhausted already
							if((std::get<1>(new_haplotype_copy_this) == 0) || (std::get<2>(new_haplotype_copy_this) != ((int)std::get<1>(new_haplotype_copy_this)->ref.length() - 1)))
							{
								assert((std::get<1>(haplotype) == 0) || (std::get<2>(haplotype) != ((int)std::get<1>(haplotype)->ref.length() - 1)));
								assert(std::get<0>(haplotype).length() == std::get<0>(new_haplotype_copy_this).length());
								if(posI == 7652900)
								{
									std::cerr << "Position " << posI << " add of length " << std::get<0>(new_haplotype_copy_this).length() << "\n"; // [@gap_structure[(posI-3) .. (posI+1)]]
								}
								assert(std::get<0>(haplotype).length() == expected_haplotype_length);
								assert(std::get<0>(new_haplotype_copy_this).length() == expected_haplotype_length);
									
								if(posI > 7650000)
								{
									//std::cerr << "A " << (&haplotype) << " vs " << &(open_haplotypes.at(outer_haplotype_I)) << "\n";
								}
								
								// perhaps these checks are not a good idea
								
								std::stringstream new_haplotype_key_str;
								new_haplotype_key_str << 	std::get<0>(new_haplotype_copy_this) << ";" <<
													std::get<1>(new_haplotype_copy_this) << ";" << 
													std::get<2>(new_haplotype_copy_this);
								std::string new_haplotype_key = new_haplotype_key_str.str();
							
								if(inner_open_haplotypes_keys.count(new_haplotype_key) == 0)
								{
									if(open_haplotypes.size() <= max_running_haplotypes_before_add)
									{  
										open_haplotypes.push_back(new_haplotype_copy_this);
										inner_open_haplotypes_keys.insert(new_haplotype_key);
									}
								}
								
								if(posI > 7650000)
								{
									//std::cerr << "B " << (&haplotype) << " vs " << &(open_haplotypes.at(outer_haplotype_I)) << "\n";
								}							
							}
							 
							if(posI > 7650000)
							{
							// 	std::cerr << "C " << (&haplotype) << " vs " << &(open_haplotypes.at(outer_haplotype_I)) << "\n";
							//	std::cerr << "Outer haplotype " << outer_haplotype_I << " / inner haplotype " << existingHaploI << ": length of " << (&haplotype) << " " << std::get<0>(haplotype).length() << "\n" << std::flush;
							}						
						}
					}

					openHaplotype& haplotype = open_haplotypes.at(outer_haplotype_I);					
					assert((std::get<1>(haplotype) == 0) || known_haplotype_pointers.count(std::get<1>(haplotype)));

					//assert(std::get<1>(haplotype) != 0);
					//std::cout << "Position " << posI << ", exit haplotype " << std::get<1>(haplotype)->query_name << " --> " << open_haplotypes.size() << " haplotypes.\n" << std::flush;
				}
			}
		}


		if(posI == 7652900)
		{
			std::cerr << "Post-exit haplotype lengths " << posI << "\n"; // [@gap_structure[(posI-3) .. (posI+1)]]
			for(openHaplotype oH2 : open_haplotypes)
			{
				std::cerr << "\t" << std::get<0>(oH2).length() << "\tconsumed until: " << std::get<2>(oH2) << ", of length " << ((std::get<1>(oH2) == 0) ? "REF" : ("nonRef " + std::get<1>(oH2)->query_name + " / length " + ItoStr(std::get<1>(oH2)->ref.length()))) << "\n";
			}
		}

		// can ignore
		// print "\tLength ", assembled_h_length, "\n";
		/*
		if(1 == 0)
		{
			print "Haplotype info:\n";
			for(existingHaploI = 0; existingHaploI <= #open_haplotypes; existingHaploI++)
			{
				print "\t", existingHaploI, "\n";
				print "\t\t", open_haplotypes[existingHaploI][0], "\n";
				print "\t\t", open_haplotypes[existingHaploI][2], "\n";
				if(open_haplotypes[existingHaploI][1])
				{
					ref_str = open_haplotypes[existingHaploI][1][0];
					haplo_str = open_haplotypes[existingHaploI][1][1];
					print "\t\t", open_haplotypes[existingHaploI][1][2], "\n";
					printFrom = open_haplotypes[existingHaploI][2];
					printFrom = 0 if(printFrom < 0);
					print "\t\t", substr(ref_str, printFrom, 10), "\n";
					print "\t\t", substr(haplo_str, printFrom, 10), "\n";
				}
				else
				{
					print "\t\tREF\n";
				}
			}
			print "\n";
		}
		*/

		// the extension step: by now all members of open_haplotypes are extensible (otherwise they were exited already)
		// we extend the first element of each haplotype with the sequence of the alignment (or the reference) we're copying from

		// ... but before we do this, make sure that everything works out length-wise
		// i.e. we populate extensions_nonRef_length, and make sure that all potential extensions have the same length
		// (and we also need this for the actual extension)
		std::set<std::string> extensions_nonRef;
		int extensions_nonRef_length = -1;
		for(openHaplotype haplotype : open_haplotypes)
		{
			std::string extension;
			int consumed_ref_start = -1;
			int consumed_ref = 0;
			std::string consumed_ref_sequence;

				
			if(std::get<1>(haplotype) == 0)
			{

			}
			else
			{
				int addIndex = 0;
				consumed_ref_start = std::get<2>(haplotype)+addIndex+1;
				do {
					int nextPosToConsume = std::get<2>(haplotype)+addIndex+1;
					if(!(nextPosToConsume < (int)std::get<1>(haplotype)->ref.length()))
					{
						std::cerr << "nextPosToConsume" << ": " << nextPosToConsume << "\n";
						std::cerr << "std::get<1>(haplotype)->ref.length()" << ": " << std::get<1>(haplotype)->ref.length() << "\n";
						std::cerr << "extension" << ": " << extension << "\n";
						std::cerr << std::flush;
					}
					assert(nextPosToConsume < (int)std::get<1>(haplotype)->ref.length());
					unsigned char refC = std::get<1>(haplotype)->ref.at(nextPosToConsume);
					consumed_ref_sequence.push_back(refC);
					if((refC != '-') && (refC != '*'))
					{
						consumed_ref++;
					}
					unsigned char hapC = std::get<1>(haplotype)->query.at(nextPosToConsume);
					extension.push_back(hapC);
					addIndex++;
				} while(consumed_ref < 1);
			}

			if(extension.length())
			{
				// push(@{extensions_nonRef{extension}}, [consumed_ref_start, consumed_ref, consumed_ref_sequence]);
				extensions_nonRef.insert(extension);
				if(extensions_nonRef_length == -1)
				{
					extensions_nonRef_length = extension.length();
				}
				assert((int)extension.length() == extensions_nonRef_length);
				/*
				unless(defined extensions_nonRef_length)
				{
					extensions_nonRef_length = length(extension);
				}
				die Dumper("Length mismatch", extension, \%extensions_nonRef, posI, "Length mismatch") unless(length(extension) == extensions_nonRef_length);
				*/
			}
		}

		// now carry out the actual extension
		std::set<std::string> extensions;
		for(openHaplotype& haplotype : open_haplotypes)
		{
			std::string extension;
			if(std::get<1>(haplotype) == 0)
			{
				std::string refExt = {(char)refC};
				if(extensions_nonRef_length != -1)
				{
					int missing = extensions_nonRef_length - refExt.length();
					assert(missing >= 0);
					std::string missingStr;
					missingStr.resize(missing, '*');
					assert((int)missingStr.length() == missing);
					refExt.append(missingStr);
				}
				extension.append(refExt);
			}
			else
			{
				int consumed_ref = 0;
				do {
					int nextPosToConsume = std::get<2>(haplotype)+1;
					assert(nextPosToConsume < (int)std::get<1>(haplotype)->ref.length());
					unsigned char refC = std::get<1>(haplotype)->ref.at(nextPosToConsume);
					if((refC != '-') && (refC != '*'))
					{
						consumed_ref++;
					}
					unsigned char hapC = std::get<1>(haplotype)->query.at(nextPosToConsume);
					extension.push_back(hapC);
					std::get<2>(haplotype)++;
				} while(consumed_ref < 1);
			}
			assert(extension.length());
			std::get<0>(haplotype).append(extension);
			extensions.insert(extension);
		}
		assert(extensions.size());
	
		// debug stuff
		// print "Extensions:\n", join("\n", map {"\t'"._."'"} keys %extensions), "\n\n";
		//#this_all_equal = ( (scalar(keys %extensions) == 0) or ((scalar(keys %extensions) == 1) and (exists extensions{refC})) );

		// IF all extensions made represent the same character
		// AND IF this charactter is equal to the reference
		// THEN we can close and output a list of variant alleles to the output VCF
		
		std::string refC_string = {(char)refC};
		bool this_all_equal = ((extensions.size() == 1) && (extensions.count(refC_string)));
		if(posI == 0)
		{
			assert(this_all_equal);
		}

		// debug stuff
		/*
		if(open_haplotypes.size() > 100)
		{
			std::cout << "Open haplotypes position " << posI << "\n";
			for(auto e : extensions)
			{
				std::cout << "\t" << e << "\n" << std::flush;
			}
 		}*/

		// carry out the closing and print to VCF
		if(this_all_equal && (posI > 0))
		{
			// close
			int ref_span = posI - start_open_haplotypes;
			assert(ref_span > 0);
			std::string reference_sequence = referenceSequence.substr(start_open_haplotypes, ref_span);
			std::set<std::string> alternativeSequences;
			std::set<std::string> uniqueRemainers;

			std::vector<openHaplotype> new_open_haplotypes;
			int open_haplotypes_before = open_haplotypes.size();
			for(openHaplotype& haplotype : open_haplotypes)
			{
				assert((int)std::get<0>(haplotype).length() >= (ref_span + 1)); // Ignore this comment: Perl pseudocoder. die Dumper("Length mismatch II", ref_span+1, length(std::get<0>(haplotype)), "Length mismatch II") unless(length(std::get<0>(haplotype)) >= (ref_span + 1));
				std::string haplotype_coveredSequence = std::get<0>(haplotype).substr(0, std::get<0>(haplotype).length()-1);
				haplotype_coveredSequence = removeGaps(haplotype_coveredSequence);
				if(haplotype_coveredSequence != reference_sequence)
				{
					alternativeSequences.insert(haplotype_coveredSequence);
				}

				// set the running component of the haplotype to the last character
				std::get<0>(haplotype) = std::get<0>(haplotype).substr(std::get<0>(haplotype).length()-1);
				assert(std::get<0>(haplotype).length() == 1);

				// unique key for this remaining haplotype to make sure we're not storing anything identical
				std::stringstream uniqueRemainerKey;
				uniqueRemainerKey << std::get<0>(haplotype) << "//" << (void*)std::get<1>(haplotype) << "//" << std::get<2>(haplotype);

				std::string k = uniqueRemainerKey.str();
				if(open_haplotypes.size() > 100)
				{
					// std::cout << k << "\n";
				}

				if(uniqueRemainers.count(k) == 0)
				{
					new_open_haplotypes.push_back(haplotype);
					uniqueRemainers.insert(k);
				}
			}

			open_haplotypes = new_open_haplotypes;
			int open_haplotypes_after = open_haplotypes.size();

			// only output to VCF if there are alternative sequences
			if(alternativeSequences.size())
			{
				bool all_alternativeAlleles_length_2 = true;
				for(auto a : alternativeSequences)
				{
					if(a.length() != 2)
						all_alternativeAlleles_length_2 = false;
				}

				// print "Starting at position start_open_haplotypes, have REF reference_sequence and alternative sequences " . join(' / ', @alternativeAlleles) . "\n";

				if((reference_sequence.length() == 2) and all_alternativeAlleles_length_2)
				{
					for(auto a : alternativeSequences)
					{
						assert(a.substr(0, 1) == reference_sequence.substr(0, 1)); // die Dumper("Some problem with supposed SNP", posI, reference_sequence, \@alternativeAlleles, "Some problem with supposed SNP") unless(substr(alt, 0, 1) eq firstRefChar);
					}

					std::vector<std::string> alternativeAlleles_reduced;
					for(auto a : alternativeSequences)
					{
						alternativeAlleles_reduced.push_back(a.substr(1,1));
					}

					outputStream <<
							referenceSequenceID << "\t" <<
							start_open_haplotypes+2 << "\t" <<
							"." << "\t" <<
							reference_sequence.substr(1,1) << "\t" <<
							join(alternativeAlleles_reduced, ",") << "\t" <<
							'.' << "\t" <<
							"PASS" << "\t" <<
							'.'
					<< "\n";
				}
				else
				{
					outputStream <<
							referenceSequenceID << "\t" <<
							start_open_haplotypes+1 << "\t" <<
							"." << "\t" <<
							reference_sequence << "\t" <<
							join(std::vector<std::string>(alternativeSequences.begin(), alternativeSequences.end()), ",") << "\t" <<
							'.' << "\t" <<
							"PASS" << "\t" <<
							'.'
					<< "\n";
				}
			}
			start_open_haplotypes = posI;

			if((n_alignments == opened_alignments) and (open_haplotypes_after == 1))
			{
				//
			}
			// std::cout << "Went from " << open_haplotypes_before << " to " << open_haplotypes_after << "\n";
		}

		// debug stuff
		if(posI == 7652900)
		{
			std::cerr << "Haplotype lengths " << posI << "\n"; // [@gap_structure[(posI-3) .. (posI+1)]]
			for(openHaplotype oH2 : open_haplotypes)
			{
				std::cerr << "\t" << std::get<0>(oH2).length() << "\tconsumed until: " << std::get<2>(oH2) << ", of length " << ((std::get<1>(oH2) == 0) ? "REF" : ("nonRef " + std::get<1>(oH2)->query_name + " / length " + ItoStr(std::get<1>(oH2)->ref.length()))) << "\n";
			}
		}

		// last_all_equal = this_all_equal;
	}
	
	std::cout << "Done.\n" << std::flush;
}



void printHaplotypesAroundPosition(const std::string& referenceSequence, const std::map<unsigned int, std::vector<startingHaplotype*>>& alignments_starting_at, int posI)
{
	std::cout << "Positions plot around " << posI << "\n" << std::flush;

	std::vector<int> positions;
	for(int i = posI - 2; i <= posI + 2; i++)
	{
		if(i >= 0)
			positions.push_back(i);
	}

	for(auto startPos : alignments_starting_at)
	{
		for(startingHaplotype* alignment : startPos.second)
		{
			assert(alignment->aligment_start_pos == startPos.first);
			int stopPos = alignment->alignment_last_pos;
			bool interesting = false;
			for(auto interestingPos : positions)
			{
				if((interestingPos >= (int)startPos.first) and (interestingPos <= stopPos))
				{
					interesting = true;
				}
			}

			if(interesting)
			{
				std::map<int, std::string> gt_per_position;

				int ref_pos = startPos.first - 1;
				std::string running_allele;

				for(int i = 0; i < (int)alignment->ref.length(); i++)
				{
					unsigned char c_ref = alignment->ref.at(i);
					unsigned char c_query = alignment->query.at(i);

					if((c_ref == '-') or (c_ref == '*'))
					{
						running_allele.push_back(c_query);
					}
					else
					{
						if(running_allele.length())
						{
							gt_per_position[ref_pos] = running_allele;
						}

						running_allele.clear();
						running_allele.push_back(c_query);
						ref_pos++;
					}
				}
				if(running_allele.length())
				{
					gt_per_position[ref_pos] = running_allele;
				}

				std::cout << "Positions " << alignment->query_name << "\n";
				for(auto interestingPos : positions)
				{
					if(gt_per_position.count(interestingPos))
					{
						std::cout << "\t" << interestingPos << "\t" << gt_per_position.at(interestingPos) << "\n";
					}
				}
			}
		}
	}

	std::cout << " -- end positions plot.\n" << std::flush;
}

vector<string> split(string input, string delimiter)
{
	vector<string> output;
	if(input.length() == 0)
	{
		return output;
	}

	if(delimiter == "")
	{
		output.reserve(input.size());
		for(unsigned int i = 0; i < input.length(); i++)
		{
			output.push_back(input.substr(i, 1));
		}
	}
	else
	{
		if(input.find(delimiter) == string::npos)
		{
			output.push_back(input);
		}
		else
		{
			int s = 0;
			int p = input.find(delimiter);

			do {
				output.push_back(input.substr(s, p - s));
				s = p + delimiter.size();
				p = input.find(delimiter, s);
			} while (p != (int)string::npos);
			output.push_back(input.substr(s));
		}
	}

	return output;
}

void eraseNL(string& s)
{
	if (!s.empty() && s[s.length()-1] == '\r') {
	    s.erase(s.length()-1);
	}
	if (!s.empty() && s[s.length()-1] == '\n') {
	    s.erase(s.length()-1);
	}
}

int StrtoI(string s)
{
	  stringstream ss(s);
	  int i;
	  ss >> i;
	  return i;
}

unsigned int StrtoUI(string s)
{
	  stringstream ss(s);
	  unsigned int i;
	  ss >> i;
	  return i;
}

string ItoStr(int i)
{
	std::stringstream sstm;
	sstm << i;
	return sstm.str();
}

string join(vector<string> parts, string delim)
{
	if(parts.size() == 0)
		return "";

	string ret = parts.at(0);

	for(unsigned int i = 1; i < parts.size(); i++)
	{
		ret.append(delim);
		ret.append(parts.at(i));
	}

	return ret;
}




std::string removeGaps(std::string in)
{
	std::string out;
	out.reserve(in.size());
	for(size_t i = 0; i < in.size(); i++)
	{
		if((in.at(i) != '_') && (in.at(i) != '-') && (in.at(i) != '*'))
		{
			out.push_back(in.at(i));
		}
	}
	return out;
}



