#include "assert.hpp"
#include "calc.hpp"
#include "hash.hpp"
#include "moves.hpp"
#include "eval.hpp"
#include "util.hpp"
#include "platform.hpp"
#include "statistics.hpp"
#include "zobrist.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <map>

short const ASPIRATION = 40;

short quiescence_search( int depth, int const max_depth, position const& p, unsigned long long hash, int current_evaluation, check_map const& check, color::type c, short alpha, short beta )
{
	int const limit = max_depth + QUIESCENCE_SEARCH;

	bool got_old_best = false;
	step_data d;

	if( hash && lookup( hash, reinterpret_cast<unsigned char * const>(&d) ) ) {
		if( d.best_move.other == 31 ) {
			if( d.evaluation == result::loss ) {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return result::loss + depth;
			}
			else if( d.evaluation == result::win ) {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return result::win - depth;
			}
			else {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return result::draw;
			}
		}
		else if( max_depth - depth - 1 >= d.best_move.other - 16 ) {
			if( alpha >= d.alpha && beta <= d.beta ) {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return d.evaluation;
			}
		}
		got_old_best = true;

		// TODO: Does this have to be cached?
	}

	if( depth >= limit && !check.check )
	{
#ifdef USE_STATISTICS
		++stats.evaluated_leaves;
#endif
		return current_evaluation;
	}

	d.beta = beta;
	d.alpha = alpha;

	bool got_quiet_move = false;

	if( got_old_best ) {
		// Not a terminal node, do this check early:
		if( depth >= limit ) {
	#ifdef USE_STATISTICS
			++stats.evaluated_leaves;
	#endif
			return current_evaluation;
		}

		position new_pos = p;
		bool captured = apply_move( new_pos, d.best_move, c );
		check_map new_check;
		calc_check_map( new_pos, static_cast<color::type>(1-c), new_check );
		short new_eval = evaluate_move( p, c, current_evaluation, d.best_move );

		short value;
		if( captured || new_check.check ) {

			unsigned long long new_hash = update_zobrist_hash( p, c, hash, d.best_move );
			value = -quiescence_search( depth + 1, max_depth, new_pos, new_hash, -new_eval, new_check, static_cast<color::type>(1-c), -beta, -alpha );
		}
		else {
			got_quiet_move = true;
			value = new_eval;
		}
		if( value > alpha ) {

			alpha = value;

			if( alpha >= beta ) {
#ifdef USE_STATISTICS
				++stats.evaluated_intermediate;
#endif
				d.evaluation = alpha;
				d.best_move.other = max_depth - depth - 1 + 16;
				store( hash, reinterpret_cast<unsigned char const* const>(&d) );
				return alpha;
			}
		}
	}

	move_info moves[200];
	move_info* pm = moves;
	calculate_moves( p, c, current_evaluation, pm, check );

	if( pm == moves ) {
		ASSERT( !got_old_best || d.best_move.other == 31 );
		if( check.check ) {
			d.best_move.other = 31;
			d.evaluation = result::loss;
			store( hash, reinterpret_cast<unsigned char const* const>(&d) );
#ifdef USE_STATISTICS
			++stats.evaluated_leaves;
#endif
			return result::loss + depth;
		}
		else {
			d.best_move.other = 31;
			d.evaluation = result::draw;
			store( hash, reinterpret_cast<unsigned char const* const>(&d) );
#ifdef USE_STATISTICS
			++stats.evaluated_leaves;
#endif
			return result::draw;
		}
	}

	if( depth >= limit ) {
#ifdef USE_STATISTICS
		++stats.evaluated_leaves;
#endif
		return current_evaluation;
	}


#ifdef USE_STATISTICS
	++stats.evaluated_intermediate;
#endif

	if( !got_old_best ) {
		d.best_move = moves->m;
	}

	++depth;

	for( move_info const* it = moves; it != pm; ++it ) {
		if( got_old_best && d.best_move == it->m ) {
			continue;
		}
		position new_pos = p;
		bool captured = apply_move( new_pos, it->m, c );
		check_map new_check;
		calc_check_map( new_pos, static_cast<color::type>(1-c), new_check );
		short value;

		if( !captured && !new_check.check ) {// && depth != max_depth  ) {
			value = it->evaluation;
			got_quiet_move = true;
		}
		else {
			unsigned long long new_hash = update_zobrist_hash( p, c, hash, it->m );
			if( got_old_best || it != moves ) {
				value = -quiescence_search( depth, max_depth, new_pos, new_hash, -it->evaluation, new_check, static_cast<color::type>(1-c), -alpha-1, -alpha );
				if( value > alpha ) {
					value = -quiescence_search( depth, max_depth, new_pos, new_hash, -it->evaluation, new_check, static_cast<color::type>(1-c), -beta, -alpha );
				}
				else {
					continue;
				}
			}
			else {
				value = -quiescence_search( depth, max_depth, new_pos, new_hash, -it->evaluation, new_check, static_cast<color::type>(1-c), -beta, -alpha );
			}
		}
		if( value > alpha ) {
			alpha = value;

			d.best_move = it->m;

			if( alpha >= beta )
				break;
		}
	}

	if( !check.check && current_evaluation > alpha && got_quiet_move ) {
		// Avoid capture line
		alpha = current_evaluation;
	}

	d.evaluation = alpha;
	d.best_move.other = d.best_move.other = max_depth - depth - 1 + 17;
	store( hash, reinterpret_cast<unsigned char const* const>(&d) );

	return alpha;
}


short quiescence_search( int depth, int const max_depth, position const& p, unsigned long long hash, int current_evaluation, bool captured, color::type c, short alpha, short beta )
{
	check_map check;
	calc_check_map( p, c, check );

	return quiescence_search( depth, max_depth, p, hash, current_evaluation, check, c, alpha, beta );
}


short step( int depth, int const max_depth, position const& p, unsigned long long hash, int current_evaluation, bool captured, color::type c, short alpha, short beta )
{
	int const limit = max_depth;

	bool got_old_best = false;
	step_data d;

	if( hash && lookup( hash, reinterpret_cast<unsigned char * const>(&d) ) ) {
		if( d.best_move.other == 31 ) {
			if( d.evaluation == result::loss ) {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return result::loss + depth;
			}
			else if( d.evaluation == result::win ) {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return result::win - depth;
			}
			else {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return result::draw;
			}
		}
		else if( (limit - depth) <= d.best_move.other - 16 ) {
			if( alpha >= d.alpha && beta <= d.beta ) {
#if USE_STATISTICS
				++stats.transposition_table_cutoffs;
#endif
				return d.evaluation;
			}
		}
		got_old_best = true;

		// TODO: Does this have to be cached?
	}

	check_map check;
	calc_check_map( p, c, check );

	if( depth >= limit ) {
		return quiescence_search( depth, max_depth, p, hash, current_evaluation, captured, c, alpha, beta );
	}

	d.beta = beta;
	d.alpha = alpha;

	if( got_old_best ) {
		position new_pos = p;
		bool captured = apply_move( new_pos, d.best_move, c );
		unsigned long long new_hash = update_zobrist_hash( p, c, hash, d.best_move );
		short new_eval = evaluate_move( p, c, current_evaluation, d.best_move );
		short value = -step( depth + 1, max_depth, new_pos, new_hash, -new_eval, captured, static_cast<color::type>(1-c), -beta, -alpha );
		if( value > alpha ) {

			alpha = value;

			if( alpha >= beta ) {
#ifdef USE_STATISTICS
				++stats.evaluated_intermediate;
#endif
				d.evaluation = alpha;
				d.best_move.other = limit - depth + 16;
				store( hash, reinterpret_cast<unsigned char const* const>(&d) );
				return alpha;
			}
		}
	}


	move_info moves[200];
	move_info* pm = moves;
	calculate_moves( p, c, current_evaluation, pm, check );

	if( pm == moves ) {
		ASSERT( !got_old_best || d.remaining_depth == -127 );
		if( check.check ) {
			d.best_move.other = 31;
			d.evaluation = result::loss;
			store( hash, reinterpret_cast<unsigned char const* const>(&d) );
#ifdef USE_STATISTICS
			++stats.evaluated_leaves;
#endif
			return result::loss + depth;
		}
		else {
			d.best_move.other = 31;
			d.evaluation = result::draw;
			store( hash, reinterpret_cast<unsigned char const* const>(&d) );
#ifdef USE_STATISTICS
			++stats.evaluated_leaves;
#endif
			return result::draw;
		}
	}

	
#ifdef USE_STATISTICS
	++stats.evaluated_intermediate;
#endif

	if( !got_old_best ) {
		d.best_move = moves->m;
	}

	++depth;

	for( move_info const* it = moves; it != pm; ++it ) {
		if( got_old_best && d.best_move == it->m ) {
			continue;
		}
		position new_pos = p;
		bool captured = apply_move( new_pos, it->m, c );
		unsigned long long new_hash = update_zobrist_hash( p, c, hash, it->m );
		short value;
		if( got_old_best || it != moves ) {
			value = -step( depth, max_depth, new_pos, new_hash, -it->evaluation, captured, static_cast<color::type>(1-c), -alpha-1, -alpha );
			if( value > alpha ) {
				value = -step( depth, max_depth, new_pos, new_hash, -it->evaluation, captured, static_cast<color::type>(1-c), -beta, -alpha );
			}
			else {
				continue;
			}
		}
		else {
			value = -step( depth, max_depth, new_pos, new_hash, -it->evaluation, captured, static_cast<color::type>(1-c), -beta, -alpha );
		}
		if( value > alpha ) {
			alpha = value;

			d.best_move = it->m;

			if( alpha >= beta )
				break;
		}
	}

	d.best_move.other = limit - depth + 17;
	d.evaluation = alpha;
	store( hash, reinterpret_cast<unsigned char const* const>(&d) );

	return alpha;
}

namespace {
class processing_thread : public thread
{
public:
	processing_thread( mutex& mtx, condition& cond )
		: mutex_(mtx), cond_(cond), finished_()
	{
	}

	// Call locked
	bool finished() {
		return finished_;
	}

	// Call locked
	short result() {
		return result_;
	}

	void process( position const& p, color::type c, move_info const& m, short max_depth, short alpha_at_prev_depth, short alpha, short beta )
	{
		p_ = p;
		c_ = c;
		m_ = m;
		max_depth_ = max_depth;
		alpha_at_prev_depth_ = alpha_at_prev_depth;
		alpha_ = alpha;
		beta_ = beta;
		finished_ = false;

		spawn();
	}

	move_info get_move() const { return m_; }

	virtual void onRun();
private:
	mutex& mutex_;
	condition& cond_;

	position p_;
	color::type c_;
	move_info m_;
	short max_depth_;
	short alpha_at_prev_depth_;
	short alpha_;
	short beta_;

	bool finished_;
	short result_;
};
}


void processing_thread::onRun()
{
	position new_pos = p_;
	bool captured = apply_move( new_pos, m_.m, c_ );
	unsigned long long hash = get_zobrist_hash( new_pos, static_cast<color::type>(1-c_) );

	// Search using aspiration window:

	short value;
	if( alpha_at_prev_depth_ != result::loss ) {
		short alpha = std::max( alpha_, static_cast<short>(alpha_at_prev_depth_ - ASPIRATION) );
		short beta = std::min( beta_, static_cast<short>(alpha_at_prev_depth_ + ASPIRATION) );

		value = -step( 1, max_depth_, new_pos, hash, -m_.evaluation, captured, static_cast<color::type>(1-c_), -beta, -alpha );
		if( value > alpha && value < beta ) {
			// Aspiration search found something sensible
			scoped_lock l( mutex_ );
			result_ = value;
			finished_ = true;
			cond_.signal( l );
			return;
		}
	}

	if( alpha_ != result::loss ) {
		value = -step( 1, max_depth_, new_pos, hash, -m_.evaluation, captured, static_cast<color::type>(1-c_), -alpha_-1, -alpha_ );
		if( value > alpha_ ) {
			value = -step( 1, max_depth_, new_pos, hash, -m_.evaluation, captured, static_cast<color::type>(1-c_), -beta_, -alpha_ );
		}
	}
	else {
		value = -step( 1, max_depth_, new_pos, hash, -m_.evaluation, captured, static_cast<color::type>(1-c_), -beta_, -alpha_ );
	}

	scoped_lock l( mutex_ );
	result_ = value;
	finished_ = true;
	cond_.signal( l );
}

typedef std::vector<std::pair<short, move_info> > sorted_moves;
void insert_sorted( sorted_moves& moves, int forecast, move_info const& m )
{
	sorted_moves::iterator it = moves.begin();
	while( it != moves.end() && it->first >= forecast ) {
		++it;
	}
	moves.insert( it, std::make_pair(forecast, m ) );
}

bool calc( position& p, color::type c, move& m, int& res, int time_limit )
{
	check_map check;
	calc_check_map( p, c, check );

	move_info moves[200];
	move_info* pm = moves;
	int current_evaluation = evaluate( p, c );
	calculate_moves( p, c, current_evaluation, pm, check );

	if( moves == pm ) {
		if( check.check ) {
			if( c == color::white ) {
				std::cerr << "BLACK WINS" << std::endl;
				res = result::loss;
			}
			else {
				std::cerr << std::endl << "WHITE WINS" << std::endl;
				res = result::win;
			}
			return false;
		}
		else {
			if( c == color::black ) {
				std::cout << std::endl;
			}
			std::cerr << "DRAW" << std::endl;
			res = result::draw;
			return false;
		}
	}

//	{
//		std::cerr << "Possible moves:";
//		move* mbegin = &moves[0];
//		move* mend = mbegin + count;
//		for( ; mbegin != mend; ++mbegin ) { // Optimize this, compiler!
//			std::cerr << " " << move_to_string( p, c, *mbegin );
//		}
//		std::cerr << std::endl;
//	}

	// Go through them sorted by previous evaluation. This way, if on time pressure,
	// we can abort processing at high depths early if needed.
	sorted_moves old_sorted;
	for( move_info const* it  = moves; it != pm; ++it ) {
		insert_sorted( old_sorted, it - moves, *it );
	}

	unsigned long long start = get_time();

	mutex mtx;

	condition cond;

	std::vector<processing_thread*> threads;
	int thread_count = 6;
	for( int t = 0; t < thread_count; ++t ) {
		threads.push_back( new processing_thread( mtx, cond ) );
	}

	unsigned long long previous_loop_duration = 0;

	short alpha_at_prev_depth = result::loss;
	for( int max_depth = 2 + (MAX_DEPTH % 2); max_depth <= MAX_DEPTH; max_depth += 2 )
	{
		unsigned long long loop_start = start;

		short alpha = result::loss;
		short beta = result::win;

		sorted_moves sorted;

		sorted_moves::const_iterator it = old_sorted.begin();
		int evaluated = 0;

		bool abort = false;
		bool done = false;
		while( !done ) {
			while( it != old_sorted.end() && !abort ) {
				int t;
				for( t = 0; t < thread_count; ++t ) {
					if( threads[t]->spawned() ) {
						continue;
					}

					threads[t]->process( p, c, it->second, max_depth, alpha_at_prev_depth, alpha, beta );

					// First one is run on its own to get a somewhat sane lower bound for others to work with.
					if( it++ == old_sorted.begin() ) {
						goto break2;
					}
					break;
				}

				if( t == thread_count ) {
					break;
				}
			}
break2:

			scoped_lock l( mtx );
			cond.wait( l );

			unsigned long long now = get_time();
			if( !abort && time_limit > 0 && (now - start) > time_limit ) {
				std::cerr << "Triggering search abort due to time limit at depth " << max_depth << std::endl;
				abort = true;
			}

			bool all_idle = true;
			for( int t = 0; t < thread_count; ++t ) {
				if( !threads[t]->spawned() ) {
					continue;
				}

				if( !threads[t]->finished() ) {
					all_idle = false;
					continue;
				}

				threads[t]->join();
				int value = threads[t]->result();
				++evaluated;
				if( value > alpha ) {
					alpha = value;
				}

				insert_sorted( sorted, value, threads[t]->get_move() );
			}

			if( all_idle ) {
				if( it == old_sorted.end() ) {
					done = true;
				}
				else if( abort ) {
					done = true;
				}
			}
		}
		if( abort ) {
			std::cerr << "Aborted with " << evaluated << " moves evaluated at current depth" << std::endl;
		}

		res = alpha;
		if( alpha < result::loss_threshold || alpha > result::win_threshold ) {
			if( max_depth < MAX_DEPTH ) {
				std::cerr << "Early break at " << max_depth << std::endl;
			}
			abort = true;
		}

		alpha_at_prev_depth = alpha;

		if( !sorted.empty() ) {
			sorted.swap( old_sorted );
		}
		if( abort ) {
			break;
		}

		unsigned long long loop_duration = get_time() - loop_start;
		if( previous_loop_duration ) {
			unsigned long long depth_factor = loop_duration / previous_loop_duration;
			//std::cerr << "Search depth increment time factor is " << depth_factor << std::endl;
		}
		previous_loop_duration = loop_duration;

	}

	std::cerr << "Candidates: " << std::endl;
	for( sorted_moves::const_iterator it = old_sorted.begin(); it != old_sorted.end(); ++it ) {
		std::cerr << move_to_string( p, c, it->second.m ) << " " << it->first << std::endl;
	}

	m = old_sorted.begin()->second.m;

	unsigned long long stop = get_time();
	print_stats( start, stop );
	reset_stats();

	for( std::vector<processing_thread*>::iterator it = threads.begin(); it != threads.end(); ++it ) {
		delete *it;
	}

	return true;
}


