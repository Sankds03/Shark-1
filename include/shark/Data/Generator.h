//===========================================================================
/*!
 * 
 *
 * \brief       Learning problems given by analytic distributions.
 * 
 * 
 * 
 *
 * \author      T. Glasmachers
 * \date        2006-2013
 *
 *
 * \par Copyright 1995-2017 Shark Development Team
 * 
 * <BR><HR>
 * This file is part of Shark.
 * <http://shark-ml.org/>
 * 
 * Shark is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published 
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Shark is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with Shark.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
//===========================================================================


#ifndef SHARK_DATA_GENERATOR_H
#define SHARK_DATA_GENERATOR_H

#include <deque>
#include <shark/Core/Threading/ThreadPool.h>
#include <shark/Data/BatchInterface.h>

namespace shark{

/// \brief The Generator generates new input data from different sources.
///
/// This class can be used to represent a large or even infinite sequence of input data, for example for data augmentation
/// purposes. The Generator supports caching to allow for parallel batch generation via the global threadpool.
/// This is in some sense a generalisation of a dataset: sampling a random batch of a dataset is a valid
/// generator. But in contrast to a dataset, the Generator does not have a concept for order or the size of a dataset.
template<class InputType>
class Generator{
public:
	typedef typename Batch<InputType>::type value_type;
	typedef typename Batch<InputType>::shape_type shape_type;
	Generator(){}
	
	Generator(std::function<value_type()> const& generator, shape_type const& shape, std::size_t cacheSize = 0)
	: m_generator(generator), m_cacheSize(cacheSize), m_shape(shape){}
	
	Generator(Generator const& other)
	: m_generator(other.m_generator)
	, m_cacheSize(other.m_cacheSize)
	, m_shape(other.m_shape){}
	
	Generator(Generator&& other)
	: m_generator(std::move(other.m_generator))
	, m_cache(std::move(other.m_cache))//for a move, we can reuse the cache
	, m_cacheSize(other.m_cacheSize)
	, m_shape(std::move(other.m_shape)){}
	
	Generator& operator=(Generator const& other){
		m_generator = other.m_generator;
		m_cacheSize = other.m_cacheSize;
		m_shape = other.m_shape;
		return *this;
	}
	
	Generator& operator=(Generator&& other){
		m_generator = std::move(other.m_generator);
		m_cache = std::move(other.m_cache);//for a move, we can reuse the cache
		m_cacheSize = other.m_cacheSize;
		m_shape = std::move(other.m_shape);
		return *this;
	}
	
	shape_type const& shape()const{
		return m_shape;
	}
	
	value_type operator()(){
		if(m_cache.size() == 0){//if caching is not used, generate 
			return m_generator();
		}
		//fill up the cache
		while(m_cache.size() < m_cacheSize +1){//+1 because we will remove the top element next
			m_cache.push_back(threading::globalThreadPool().execute_async(m_generator));
		}
		//query top element
		std::future<value_type> result = std::move(m_cache.front());
		m_cache.pop_front();
		//Return when ready
		return result.get();
	}
	
	std::function<value_type()> const& generatingFunction() const{
		return m_generator;
	}
	
	std::size_t cacheSize() const{
		return m_cacheSize;
	}
	
private:
	std::function<value_type()> m_generator;
	std::deque<std::future<value_type> > m_cache;
	std::size_t m_cacheSize;
	shape_type m_shape;
};

///\brief Transforms the output of the generator using a function f and returns the transformed generator.
///
/// Each batch is therefore computed as f(gen())
/// The shape of the result has to be provided as separate argument
/// \param gen The generator to transform
/// \param shape The shape of the resulting data
/// \param model the model that is applied element by element
template<class T, class Functor>
Generator<typename detail::TransformedBatchElement<Functor,typename Batch<T>::type>::element_type >
transform(
	Generator<T> const& gen, Functor f,
	typename detail::TransformedBatchElement<Functor,typename Batch<T>::type>::shape_type const& shape
){
	typedef typename detail::TransformedBatchElement<Functor,typename Batch<T>::type>::element_type ResultType;
	auto oldGen = gen.generatingFunction();
	return Generator<ResultType>([oldGen, f]{return f(oldGen());}, shape, gen.cacheSize());
}

///\brief For a generator returning pairs of inputs and labels, transforms the inputs using a function f and returns the transformed generator.
///
/// Each batch is therefore computed as (f(gen().input), gen().label)
/// The shape of the resulting inputs has to be provided as separate argument
/// \param gen The generator to transform
/// \param shape The shape of the resulting data
/// \param model the model that is applied element by element
template<class I, class L, class Functor>
Generator<InputLabelPair<
	typename detail::TransformedBatchElement<Functor,typename Batch<I>::type>::element_type, L
> >
transformInputs(
	Generator<InputLabelPair<I, L> >const& gen, Functor f,
	typename detail::TransformedBatchElement<Functor,typename Batch<I>::type>::shape_type const& shape
){
	typedef typename detail::TransformedBatchElement<Functor,typename Batch<I>::type>::element_type ResultType;
	typedef typename Batch<InputLabelPair<ResultType, L> >::type ResultBatch;
	auto oldGen = gen.generatingFunction();
	auto newGen = [oldGen, f]{
		auto oldBatch = oldGen();
		return ResultBatch{f(std::move(oldBatch.input)),std::move(oldBatch.label)};
	};
	return Generator<InputLabelPair<ResultType, L> >(newGen, shape, gen.cacheSize());
}

///\brief For a generator returning pairs of inputs and labels, transforms the labels using a function f and returns the transformed generator.
///
/// Each batch is therefore computed as (gen().input, f(gen().label))
/// The shape of the resulting labels has to be provided as separate argument
/// \param gen The generator to transform
/// \param shape The shape of the resulting data
/// \param model the model that is applied element by element
template<class I, class L, class Functor>
Generator<InputLabelPair<
	I, typename detail::TransformedBatchElement<Functor,typename Batch<L>::type>::element_type
> >
transformLabels(
	Generator<InputLabelPair<I, L> >const& gen, Functor f,
	typename detail::TransformedBatchElement<Functor,typename Batch<L>::type>::shape_type const& shape
){
	typedef typename detail::TransformedBatchElement<Functor,typename Batch<L>::type>::element_type ResultType;
	typedef typename Batch<InputLabelPair<I,ResultType> >::type ResultBatch;
	auto oldGen = gen.generatingFunction();
	auto newGen = [oldGen, f]{
		auto oldBatch = oldGen();
		return ResultBatch{std::move(oldBatch.input), f(std::move(oldBatch.label))};
	};
	return Generator<InputLabelPair<I, ResultType> >(newGen, shape, gen.cacheSize());
}

}
#endif