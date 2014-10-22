// AmpProto.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define _USE_MATH_DEFINES

#include <assert.h>
#include <stdint.h>
#include <math.h>

#include <iostream>
#include <chrono>
#include <vector>
#include <fstream>
#include <iomanip>
#include <random>
#include <functional>
#include <algorithm>

#include <amp.h>
using namespace concurrency;


typedef float out_type;
typedef std::vector<out_type> MatrixType;

template<typename T>
auto NoiseFunc(T trace_noise) -> std::function<T()>
{
	std::random_device rd;
	std::mt19937 eng(rd());
	if (trace_noise < 0) trace_noise *= -1.0;
	std::normal_distribution<T> dist(0, trace_noise);

	return std::bind(dist, std::ref(eng));
}


void SignalNoisedCpp(uint32_t dataLength, double trace_ampl, double trace_noise)
{
	// Create an init signal
	uint32_t i = 0;
	std::vector<out_type> funcbucket(dataLength);
	std::generate(funcbucket.begin(), funcbucket.end(), [&]()
	{
		double arg = (1.0 * i++ / dataLength) * 10 * 2 * M_PI;
		double res = sin(arg);
		res *= trace_ampl;
		return static_cast<out_type>(res);
	});

	std::random_device rd;
	std::mt19937 eng(rd());
	if (trace_noise < 0) trace_noise *= -1.0;
	std::normal_distribution<double> dist(0, trace_noise);

	auto noise_func = std::bind(dist, std::ref(eng));


	//auto noise_func = NoiseFunc(trace_noise);

	std::vector<out_type> tracebucket(dataLength);
	for (uint32_t i = 0; i < dataLength; i++)
	{
		// apply noise to signal
		std::transform(funcbucket.begin(), funcbucket.end(), tracebucket.begin(), [&](out_type e)
		{
			return e + noise_func();
		});
	}

}

MatrixType AllocMatrix(size_t rows, size_t columns)
{
	return MatrixType(rows*columns);
}

MatrixType GetNoiseMatrix(size_t rows, size_t columns, out_type trace_noise)
{
	std::random_device rd;
	std::mt19937 eng(rd());
	if (trace_noise < 0) trace_noise *= -1.0;

	std::uniform_real<out_type> dist(0, trace_noise);

	auto noise_func = std::bind(dist, std::ref(eng));

	auto retMatrix = AllocMatrix(rows, columns);
	std::generate(retMatrix.begin(), retMatrix.end(), noise_func);

	return retMatrix;
}

MatrixType MultiplyCpp(const MatrixType &aMatrix, const MatrixType &bMatrix)
{
	int mSize = static_cast<int>(sqrt(aMatrix.size()));
	size_t rows = mSize;
	size_t columns = mSize;
	size_t innerSize = mSize;

	auto productMatrix = AllocMatrix(rows, columns);

	for (size_t row = 0; row < rows; row++) 
	{
		auto idxRow = row*columns;
		for (size_t col = 0; col < columns; col++)
		{
			// Multiply the row of A by the column of B to get the row, column of product.
			for (size_t inner = 0; inner < innerSize; inner++)
			{
				productMatrix[idxRow + col] += aMatrix[idxRow + inner] * bMatrix[inner*columns + col];
			}
		}
	}

	return productMatrix;
}

MatrixType MultiplyAMP(const MatrixType &aMatrix, const MatrixType &bMatrix)
{
	int mSize = static_cast<int>(sqrt(aMatrix.size()));
	size_t rows = mSize;
	size_t columns = mSize;
	size_t innerSize = mSize;

	auto productMatrix = AllocMatrix(rows, columns);

	array_view<const MatrixType::value_type, 2> a(rows, columns, aMatrix.data());
	array_view<const MatrixType::value_type, 2> b(rows, columns, bMatrix.data());
	array_view<MatrixType::value_type, 2> product(rows, columns, productMatrix.data());

	parallel_for_each(
		product.extent,
		[=](index<2> idx) restrict(amp) 
		{
			int row = idx[0];
			int col = idx[1];
			for (size_t inner = 0; inner < rows; inner++) 
			{
				product[idx] += a(row, inner) * b(inner, col);
			}
		}
	);

	product.synchronize();

	return productMatrix;
}

int _tmain(int argc, _TCHAR* argv[])
{
	std::chrono::steady_clock::time_point start, end;

	start = std::chrono::steady_clock::now();
	SignalNoisedCpp(100, 1.0, 0.1);
	end = std::chrono::steady_clock::now();

	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::cout << "Test duration: " << duration << " ms." << std::endl;

	
	auto mA = GetNoiseMatrix(500, 500, 5); // std::vector<out_type>({ 0, 1, 2, 3 });
	auto mB = GetNoiseMatrix(500, 500, 5); // std::vector<out_type>({ 0, 1, 2, 3 });

	start = std::chrono::steady_clock::now();
	auto product = MultiplyCpp(mA, mB);
	end = std::chrono::steady_clock::now();

	duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::cout << "C++ mul duration: " << duration << " ms." << std::endl;

	start = std::chrono::steady_clock::now();
	auto product2 = MultiplyAMP(mA, mB);
	end = std::chrono::steady_clock::now();

	duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::cout << "AMP mul duration: " << duration << " ms." << std::endl;

	getchar();

	return 0;
}

