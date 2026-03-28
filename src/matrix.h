#ifndef MATRIX_H
#define MATRIX_H

#include <iostream>
#include <stdexcept>
#include <vector>

class Matrix {
public:
    int rows;
    int cols;
    std::vector<double> data;

    Matrix(int r, int c);
    Matrix(int r, int c, double val);

    double& operator()(int r, int c);
    double operator()(int r, int c) const;

    void print() const;

    Matrix operator+(const Matrix& other) const;
    Matrix operator-(const Matrix& other) const;
    Matrix operator*(double scalar) const;

    Matrix transpose() const;
    Matrix dot(const Matrix& other) const;

    static Matrix zeros(int r, int c);
};

#endif
