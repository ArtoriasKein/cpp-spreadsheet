#include "cell.h"

#include <cassert>
#include <iostream>
#include <string>
#include <optional>
#include <cmath>    // for std::isfinite()


// Реализуйте следующие методы
Cell::Cell(SheetInterface& sheet)
    : sheet_(sheet)
{}

Cell::~Cell()
{
    if (impl_)
    {
        // Указатель существует, удаляем его
        impl_.reset(nullptr);    // Reset вызывает дефолтный deleter
    }
}

void Cell::Set(const std::string& text)
{
    using namespace std::literals;

    // Отдельная обработка случая пустой строки
    if (text.empty())
    {
        impl_ = std::make_unique<EmptyImpl>();
        return;
    }

    // Все, что НЕ начинается с '=' или состоит ТОЛЬКО из одного знака '=' является текстом
    if (text[0] != FORMULA_SIGN || (text[0] == FORMULA_SIGN && text.size() == 1))
    {
        // Обработка экранирующих символов производится в конструкторе TextImpl
        impl_ = std::make_unique<TextImpl>(text);
        return;
    }

    // У нас формула. Отрезаем лидирующий знак '='
    //std::string formula_text = text.erase(0, 1);    // Вместо обработки строки используем итераторы по месту
    // Пытаемся получить указатель на объект формулы
    try
    {
        //impl_ = std::make_unique<FormulaImpl>(formula_text);    // Вместо вспомогательной строки используем итераторы
        impl_ = std::make_unique<FormulaImpl>(sheet_, std::string{ text.begin() + 1, text.end() });

    }
    catch (...)
    {
        std::string fe_msg = "Formula parsing error"s;
        throw FormulaException(fe_msg);
    }
}

void Cell::Clear()
{
    // Reset вызывает дефолтный deleter
    //impl_.reset(nullptr);

    // Создаем новую реализацию типа "пустая ячейка"
    impl_ = std::make_unique<EmptyImpl>();
}

Cell::Value Cell::GetValue() const
{
    return impl_->IGetValue();
}

std::string Cell::GetText() const
{
    return impl_->IGetText();
}

std::vector<Position> Cell::GetReferencedCells() const
{
    return impl_.get()->IGetReferencedCells();
}

bool Cell::IsCyclicDependent(const Cell* start_cell_ptr, const Position& end_pos) const
{
    // Проверяем все зависимые ячейки
    for (const auto& referenced_cell_pos : GetReferencedCells())
    {

        // Позиция ячейки из списка зависимости текущей совпадает с конечной
        // (циклическая зависимость найдена)
        if (referenced_cell_pos == end_pos)
        {
            return true;
        }

        // Пытаемся получить указатель на очередную ячейку из списка зависимости текущей
        const Cell* ref_cell_ptr = dynamic_cast<const Cell*>(sheet_.GetCell(referenced_cell_pos));

        if (!ref_cell_ptr)
        {
            // По заданию, ссылаться на несуществующие ячейки можно. У нас такой случай,
            // создаем EmptyImpl для текущей referenced_cell_pos
            sheet_.SetCell(referenced_cell_pos, "");
            ref_cell_ptr = dynamic_cast<const Cell*>(sheet_.GetCell(referenced_cell_pos));
        }

        // Указатели начальной и текущей ячеек совпали
        // (циклическая зависимость найдена)
        if (start_cell_ptr == ref_cell_ptr)
        {
            return true;
        }

        // Рекурсивно вызываем проверку для ячеек из списка зависимости текущей. 
        // start_cell_ptr и end_pos передаем без изменений
        if (ref_cell_ptr->IsCyclicDependent(start_cell_ptr, end_pos))
        {
            return true;
        }
    }

    // Если мы здесь, циклические зависимости не найдены
    return false;
}

void Cell::InvalidateCache()
{
    impl_->IInvalidateCache();
}

bool Cell::IsCacheValid() const
{
    return impl_->ICached();
}

CellType Cell::EmptyImpl::IGetType() const
{
    return CellType::EMPTY;
}

CellInterface::Value Cell::EmptyImpl::IGetValue() const
{
    return 0.0;
}

std::string Cell::EmptyImpl::IGetText() const
{
    using namespace std::literals;

    return ""s;
}

std::vector<Position> Cell::EmptyImpl::IGetReferencedCells() const
{
    return {};
}

void Cell::EmptyImpl::IInvalidateCache()
{
    return;
}

bool Cell::EmptyImpl::ICached() const
{
    return true;
}

Cell::TextImpl::TextImpl(std::string text)
    : cell_text_(std::move(text))
{
    // Проверяем наличие экранирующих символов
    // text.size() != 0  ,  эта проверка произведена в Cell.Set()
    if (cell_text_[0] == ESCAPE_SIGN)
    {
        escaped_ = true;
    }
}

CellType Cell::TextImpl::IGetType() const
{
    return CellType::TEXT;
}

CellInterface::Value Cell::TextImpl::IGetValue() const
{
    if (escaped_)
    {
        // Возвращаем без апострофа
        return cell_text_.substr(1, cell_text_.size() - 1);
    }
    else
    {
        return cell_text_;
    }
}

std::string Cell::TextImpl::IGetText() const
{
    return cell_text_;
}

std::vector<Position> Cell::TextImpl::IGetReferencedCells() const
{
    return {};
}

void Cell::TextImpl::IInvalidateCache()
{
    return;
}

bool Cell::TextImpl::ICached() const
{
    return true;
}

Cell::FormulaImpl::FormulaImpl(SheetInterface& sheet, std::string formula)
    : sheet_(sheet), formula_(ParseFormula(formula))
{}

CellType Cell::FormulaImpl::IGetType() const
{
    return CellType::FORMULA;
}

CellInterface::Value Cell::FormulaImpl::IGetValue() const
{

    // Все расчеты производим только если кэш невалиден
    if (!cached_value_)
    {
        FormulaInterface::Value result = formula_->Evaluate(sheet_);
        if (std::holds_alternative<double>(result))
        {
            // Вычисление произведено успешно
            if (std::isfinite(std::get<double>(result)))
            {
                return std::get<double>(result);
            }
            else
            {
                return FormulaError(FormulaError::Category::Div0);
            }
        }

        // Если мы здесь, вычисление закончилось ошибкой. Возвращаем ее
        return std::get<FormulaError>(result);
    }

    // Если мы здесь, то кэш вычислен и валиден. Возвращаем его значение.
    return *cached_value_;
}

std::string Cell::FormulaImpl::IGetText() const
{
    return { FORMULA_SIGN + formula_->GetExpression() };
}

std::vector<Position> Cell::FormulaImpl::IGetReferencedCells() const
{
    return formula_.get()->GetReferencedCells();
}

void Cell::FormulaImpl::IInvalidateCache()
{
    cached_value_.reset();
}

bool Cell::FormulaImpl::ICached() const
{
    return cached_value_.has_value();
}
