#include "training_demo.h"

#include <iomanip>
#include <iostream>

#include "../matrix.h"

namespace {

void run_matrix_demo() {
    Matrix A(2, 3);
    A(0, 0) = 1.0;
    A(0, 1) = 2.0;
    A(0, 2) = 3.0;
    A(1, 0) = 4.0;
    A(1, 1) = 5.0;
    A(1, 2) = 6.0;

    Matrix E(3, 1);
    E(0, 0) = 1.0;
    E(1, 0) = 0.5;
    E(2, 0) = -1.0;

    std::cout << "Matrix A:" << std::endl;
    A.print();

    std::cout << "\nA transpose:" << std::endl;
    A.transpose().print();

    std::cout << "\nA dot E:" << std::endl;
    A.dot(E).print();
}

Matrix predict_linear(const Matrix& inputs, const Matrix& weights, double bias) {
    Matrix predictions = inputs.dot(weights);
    for (int i = 0; i < predictions.rows; ++i) {
        predictions(i, 0) += bias;
    }
    return predictions;
}

double mean_squared_error(const Matrix& predictions, const Matrix& targets) {
    if (predictions.rows != targets.rows || predictions.cols != targets.cols) {
        throw std::invalid_argument("Prediction and target shape mismatch");
    }

    double total = 0.0;
    for (int i = 0; i < predictions.rows; ++i) {
        const double diff = predictions(i, 0) - targets(i, 0);
        total += diff * diff;
    }
    return total / predictions.rows;
}

}  // namespace

void run_training_demo() {
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Mini deep learning demo in C++" << std::endl;
    std::cout << "-----------------------------" << std::endl;

    run_matrix_demo();

    Matrix inputs(4, 1);
    inputs(0, 0) = 1.0;
    inputs(1, 0) = 2.0;
    inputs(2, 0) = 3.0;
    inputs(3, 0) = 4.0;

    Matrix targets(4, 1);
    targets(0, 0) = 3.0;
    targets(1, 0) = 5.0;
    targets(2, 0) = 7.0;
    targets(3, 0) = 9.0;

    Matrix weights(1, 1);
    weights(0, 0) = 0.0;
    double bias = 0.0;

    const double learning_rate = 0.05;
    const int epochs = 500;

    std::cout << "\nTraining demo: learn y = 2x + 1" << std::endl;
    std::cout << "Initial weight = " << weights(0, 0)
              << ", bias = " << bias << std::endl;

    for (int epoch = 1; epoch <= epochs; ++epoch) {
        const Matrix predictions = predict_linear(inputs, weights, bias);
        const Matrix error = predictions - targets;

        Matrix grad_weight = inputs.transpose().dot(error) * (2.0 / inputs.rows);

        double grad_bias = 0.0;
        for (int i = 0; i < error.rows; ++i) {
            grad_bias += error(i, 0);
        }
        grad_bias *= 2.0 / inputs.rows;

        weights(0, 0) -= learning_rate * grad_weight(0, 0);
        bias -= learning_rate * grad_bias;

        if (epoch == 1 || epoch % 50 == 0 || epoch == epochs) {
            std::cout << "Epoch " << std::setw(3) << epoch
                      << " | loss = " << mean_squared_error(predictions, targets)
                      << " | weight = " << weights(0, 0)
                      << " | bias = " << bias << std::endl;
        }
    }

    Matrix test_input(1, 1);
    test_input(0, 0) = 6.0;
    const Matrix test_prediction = predict_linear(test_input, weights, bias);

    std::cout << "\nLearned function: y = " << weights(0, 0)
              << " * x + " << bias << std::endl;
    std::cout << "Prediction for x = 6: " << test_prediction(0, 0) << std::endl;
}
