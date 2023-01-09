#include "sheet.h"

#include "cell.h"
#include "common.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>

using namespace std::literals;

Sheet::~Sheet()
{}

void Sheet::SetCell(Position pos, const std::string& text)
{
    // Невалидные позиции не обрабатываем
    if (!pos.IsValid())
    {
        throw InvalidPositionException("Invalid position for SetCell()");
    }

    // Выделяем память под ячейку, если нужно
    Touch(pos);
    // Получаем указатель на ячейку для текущего листа
    auto cell = GetCell(pos);

    if (cell)
    {
        // Ячейка уже существует.
        // Сохраним старое содержимое на случай ввода некорректной формулы.
        // По заданию мы должны откатить изменения в этом случае.
        std::string old_text = cell->GetText();

        // Инвалидируем кэш ячейки и зависимых от нее... 
        InvalidateCell(pos);
        // ... и удаляем зависимости
        DeleteDependencies(pos);
        // Очищаем старое содержимое ячейки (не сам unique_ptr, а содержание ячейки по указанному адресу)
        dynamic_cast<Cell*>(cell)->Clear();

        dynamic_cast<Cell*>(cell)->Set(text);
        // Проверяем на циклические зависимости новое содержимое cell
        if (dynamic_cast<Cell*>(cell)->IsCyclicDependent(dynamic_cast<Cell*>(cell), pos))
        {
            // Есть циклическая зависимость. Откат изменений
            dynamic_cast<Cell*>(cell)->Set(std::move(old_text));
            throw CircularDependencyException("Circular dependency detected!");
        }

        // Сохраняем зависимости
        for (const auto& ref_cell : dynamic_cast<Cell*>(cell)->GetReferencedCells())
        {
            AddDependentCell(ref_cell, pos);
        }
    }
    else
    {
        // Новая ячейка (nullptr). Нужна проверка изменений Printable Area в конце
        auto new_cell = std::make_unique<Cell>(*this);
        new_cell->Set(text);

        // Проверяем циклические ссылки
        if (new_cell.get()->IsCyclicDependent(new_cell.get(), pos))
        {
            throw CircularDependencyException("Circular dependency detected!");
        }

        // К настоящему моменту валидность формулы, позиции и отсутствие
        // циклических зависимостей проверены.
        // Переходим к модификации Sheet.

        // Проходим по вектору ячеек из формулы и добавляем
        // для каждой из них нашу ячейку как зависимую
        for (const auto& ref_cell : new_cell.get()->GetReferencedCells())
        {
            AddDependentCell(ref_cell, pos);
        }

        // Заменяем unique_ptr с nullptr из sheet_  на новый указатель
        sheet_.at(pos.row).at(pos.col) = std::move(new_cell);
        UpdatePrintableSize();
    }
}

const CellInterface* Sheet::GetCell(Position pos) const
{
    // Невалидные позиции не обрабатываем
    if (!pos.IsValid())
    {
        throw InvalidPositionException("Invalid position for GetCell()");
    }

    // Если память для ячейки выделена...
    if (CellExists(pos))
    {
        //  ...и ее указатель не nullptr...
        if (sheet_.at(pos.row).at(pos.col))
        {
            // ...возвращаем его
            return sheet_.at(pos.row).at(pos.col).get();
        }
    }

    // Для любых несуществующих ячеек возвращаем просто nullptr
    return nullptr;
}

CellInterface* Sheet::GetCell(Position pos)
{
    // Невалидные позиции не обрабатываем
    if (!pos.IsValid())
    {
        throw InvalidPositionException("Invalid position for GetCell()");
    }

    // Если память для ячейки выделена...
    if (CellExists(pos))
    {
        //  ...и ее указатель не nullptr...
        if (sheet_.at(pos.row).at(pos.col))
        {
            // ...возвращаем его
            return sheet_.at(pos.row).at(pos.col).get();
        }
    }

    // Для любых несуществующих ячеек возвращаем просто nullptr
    return nullptr;
}

void Sheet::ClearCell(Position pos)
{
    // Невалидные позиции не обрабатываем
    if (!pos.IsValid())
    {
        throw InvalidPositionException("Invalid position for ClearCell()");
    }

    if (CellExists(pos))
    {
        sheet_.at(pos.row).at(pos.col).reset();       // Удаляет содержимое ячейки

         // pos.row/col 0-based          max_row/col 1-based
        if ((pos.row + 1 == max_row_) || (pos.col + 1 == max_col_))
        {
            // Удаленная ячейка была на границе Printable Area. Нужен перерасчет
            area_is_valid_ = false;
            UpdatePrintableSize();
        }
    }
}

Size Sheet::GetPrintableSize() const
{
    if (area_is_valid_)
    {
        return Size{ max_row_, max_col_ };
    }
    // Бросаем исключение
    throw InvalidPositionException("The size of printable area has not been updated");
}

void Sheet::PrintValues(std::ostream& output) const
{
    for (int x = 0; x < max_row_; ++x)
    {
        bool need_separator = false;
        // Проходим по всей ширине Printable area
        for (int y = 0; y < max_col_; ++y)
        {
            // Проверка необходимости печати разделителя
            if (need_separator)
            {
                output << '\t';
            }
            need_separator = true;

            // Если мы не вышли за пределы вектора И ячейка не nullptr
            if ((y < static_cast<int>(sheet_.at(x).size())) && sheet_.at(x).at(y))
            {
                // Ячейка существует
                auto value = sheet_.at(x).at(y)->GetValue();
                if (std::holds_alternative<std::string>(value))
                {
                    output << std::get<std::string>(value);
                }
                if (std::holds_alternative<double>(value))
                {
                    output << std::get<double>(value);
                }
                if (std::holds_alternative<FormulaError>(value))
                {
                    output << std::get<FormulaError>(value);
                }
            }
        }
        // Разделение строк
        output << '\n';
    }
}

void Sheet::PrintTexts(std::ostream& output) const
{
    for (int x = 0; x < max_row_; ++x)
    {
        bool need_separator = false;
        // Проходим по всей ширине Printable area
        for (int y = 0; y < max_col_; ++y)
        {
            // Проверка необходимости печати разделителя
            if (need_separator)
            {
                output << '\t';
            }
            need_separator = true;

            // Если мы не вышли за пределы вектора И ячейка не nullptr
            if ((y < static_cast<int>(sheet_.at(x).size())) && sheet_.at(x).at(y))
            {
                // Ячейка существует
                output << sheet_.at(x).at(y)->GetText();
            }
        }
        // Разделение строк
        output << '\n';
    }
}

void Sheet::InvalidateCell(const Position& pos)
{
    // Для всех зависимых ячеек рекурсивно инвалидируем кэш
    for (const auto& dependent_cell : GetDependentCells(pos))
    {
        auto cell = GetCell(dependent_cell);
        // InvalidateCache() есть только у Cell, приводим указатель
        dynamic_cast<Cell*>(cell)->InvalidateCache();
        InvalidateCell(dependent_cell);
    }
}

void Sheet::AddDependentCell(const Position& main_cell, const Position& dependent_cell)
{
    // При отсутствии записи для main_cell создаем ее через []
    cells_dependencies_[main_cell].insert(dependent_cell);
}

const std::set<Position> Sheet::GetDependentCells(const Position& pos)
{
    if (cells_dependencies_.count(pos) != 0)
    {
        // Есть такой ключ в словаре зависимостей. Возвращаем значение
        return cells_dependencies_.at(pos);
    }

    // Если мы здесь, от ячейки pos никто не зависит
    return {};
}

void Sheet::DeleteDependencies(const Position& pos)
{
    cells_dependencies_.erase(pos);
}

void Sheet::UpdatePrintableSize()
{
    max_row_ = 0;
    max_col_ = 0;

    // Сканируем ячейки, пропуская nullptr
    for (int x = 0; x < static_cast<int>(sheet_.size()); ++x)
    {
        for (int y = 0; y < static_cast<int>(sheet_.at(x).size()); ++y)
        {
            if (sheet_.at(x).at(y))
            {
//                max_row_ = (max_row_ < (x + 1) ? x + 1 : max_row_);
//                max_col_ = (max_col_ < (y + 1) ? y + 1 : max_col_);
                max_row_ = std::max(max_row_, x + 1);
                max_col_ = std::max(max_col_, y + 1);
            }
        }
    }

    // Перерасчет произведен
    area_is_valid_ = true;
}

bool Sheet::CellExists(Position pos) const
{
    return (pos.row < static_cast<int>(sheet_.size())) && (pos.col < static_cast<int>(sheet_.at(pos.row).size()));
}

void Sheet::Touch(Position pos)
{
    // Невалидные позиции не обрабатываем
    if (!pos.IsValid())
    {
        return;
    }

    // size() 1-based          pos.row/col 0-based          sheet_[] 0-based

    // Если элементов в векторе строк меньше, чем номер строки в pos.row...
    if (static_cast<int>(sheet_.size()) < (pos.row + 1))
    {
        // ... резервируем и инициализируем nullptr элементы вплоть до строки pos.row
        sheet_.reserve(pos.row + 1);
        sheet_.resize(pos.row + 1);
    }

    // Если элементов в векторе столбцов меньше, чем номер столбца в pos.col...
    if (static_cast<int>(sheet_.at(pos.row).size()) < (pos.col + 1))
    {
        // ... резервируем и инициализируем nullptr элементы вплоть до столбца pos.col
        sheet_.at(pos.row).reserve(pos.col + 1);
        sheet_.at(pos.row).resize(pos.col + 1);
    }
}

// Создаёт готовую к работе пустую таблицу. Объявление в common.h
std::unique_ptr<SheetInterface> CreateSheet()
{
    return std::make_unique<Sheet>();
}
