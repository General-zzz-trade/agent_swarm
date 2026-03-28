#include "matrix.h"

Matrix::Matrix(int r, int c) : rows(r), cols(c), data(r * c, 0.0) {
    if (r <= 0 || c <= 0) {
        throw std::invalid_argument("Matrix dimensions must be positive");
    }
}

Matrix::Matrix(int r, int c, double val) : rows(r), cols(c), data(r * c, val) {
    if (r <= 0 || c <= 0) {
        throw std::invalid_argument("Matrix dimensions must be positive");
    }
}

double& Matrix::operator()(int r, int c) {
    if (r < 0 || r >= rows || c < 0 || c >= cols) {
        throw std::out_of_range("Matrix index out of range");
    }
    return data[r * cols + c];
}

double Matrix::operator()(int r, int c) const {
    if (r < 0 || r >= rows || c < 0 || c >= cols) {
        throw std::out_of_range("Matrix index out of range");
    }
    return data[r * cols + c];
}

void Matrix::print() const {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            std::cout << (*this)(i, j) << " ";
        }
        std::cout << std::endl;
    }
}

Matrix Matrix::operator+(const Matrix& other) const {
    if (rows != other.rows || cols != other.cols) {
        throw std::invalid_argument("Matrix addition shape mismatch");
    }

    Matrix result(rows, cols);
    for (int i = 0; i < rows * cols; ++i) {
        result.data[i] = data[i] + other.data[i];
    }
    return result;
}

Matrix Matrix::operator-(const Matrix& other) const {
    if (rows != other.rows || cols != other.cols) {
        throw std::invalid_argument("Matrix subtraction shape mismatch");
    }

    Matrix result(rows, cols);
    for (int i = 0; i < rows * cols; ++i) {
        result.data[i] = data[i] - other.data[i];
    }
    return result;
}

Matrix Matrix::operator*(double scalar) const {
    Matrix result(rows, cols);
    for (int i = 0; i < rows * cols; ++i) {
        result.data[i] = data[i] * scalar;
    }
    return result;
}

Matrix Matrix::transpose() const {
    Matrix result(cols, rows);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            result(j, i) = (*this)(i, j);
        }
    }
    return result;
}

Matrix Matrix::dot(const Matrix& other) const {
    if (cols != other.rows) {
        throw std::invalid_argument("Matrix multiplication shape mismatch");
    }

    Matrix result(rows, other.cols, 0.0);

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < other.cols; ++j) {
            double sum = 0.0;
            for (int k = 0; k < cols; ++k) {
                sum += (*this)(i, k) * other(k, j);
            }
            result(i, j) = sum;
        }
    }

    return result;
}

Matrix Matrix::zeros(int r, int c) {
    return Matrix(r, c, 0.0);
}
