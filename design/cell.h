#pragma once

#include "common.h"
#include "formula.h"

//#include <set>    // для dependent_cells_
#include <optional>
#include <functional>     // из прекода к заданию
#include <unordered_set>  // из прекода к заданию

// Тип ячейки
enum class CellType
{
    EMPTY,    // default type on cell creation
    TEXT,
    FORMULA,
    ERROR
};

class Cell : public CellInterface {
public:
    Cell(SheetInterface& sheet);    // Конструктор теперь принимает ссылку на лист таблицы
    ~Cell();

    void Set(const std::string& text);
    void Clear();

    CellInterface::Value GetValue() const override;
    std::string GetText() const override;
    std::vector<Position> GetReferencedCells() const override;

    // Метод проверяет циклическую зависимость start_cell_ptr от end_pos
    bool IsCyclicDependent(const Cell* start_cell_ptr, const Position& end_pos) const;
    // Метод сбрасывает содержимое кэша ячейки
    void InvalidateCache();
    // Метод проверяет кэшированы ли данные в ячейке
    bool IsCacheValid() const;

private:
    class Impl;    // Forward declaration

    std::unique_ptr<Impl> impl_;    // Указатель на класс-реализацию ячейки
    SheetInterface& sheet_;         // Ссылка на лист таблицы, которому принадлежит ячейка
    //Перенесено в Sheet
    //std::unordered_set<Position> dependent_cells_;    // Перечень ячеек, завясящих от текущей


    // ИНТЕРФЕЙС - ячейка таблицы
    class Impl
    {
    public:
        virtual ~Impl() = default;
        virtual CellType IGetType() const = 0;
        virtual CellInterface::Value IGetValue() const = 0;
        virtual std::string IGetText() const = 0;

        virtual std::vector<Position> IGetReferencedCells() const = 0;    // Получить список ячеек, от которых зависит текущая
        virtual void IInvalidateCache() = 0;     // Инвалидация кэша
        virtual bool ICached() const = 0;        // Проверка валидности кэша
    };

    // Класс "Пустая ячейка"
    class EmptyImpl : public Impl
    {
    public:
        EmptyImpl() = default;
        CellType IGetType() const override;
        CellInterface::Value IGetValue() const override;    // Возвразщает пустую строку
        std::string IGetText() const override;              // Возвразщает пустую строку

        std::vector<Position> IGetReferencedCells() const override; 
        void IInvalidateCache() override;
        bool ICached() const override;
    };

    // Класс "Ячейка с текстом"
    class TextImpl : public Impl
    {
    public:
        explicit TextImpl(std::string text);
        CellType IGetType() const override;
        CellInterface::Value IGetValue() const override;    // Возвращает очищенный текст ячейки
        std::string IGetText() const override;              // Возвращает текст ячейки со всеми экранирующими символами

        std::vector<Position> IGetReferencedCells() const override;
        void IInvalidateCache() override;
        bool ICached() const override;
    private:
        std::string cell_text_;
        bool escaped_ = false;    // Экранировано ли содержимое esc-символом (апострофом)
    };

    // Класс "Ячейка с формулой"
    class FormulaImpl : public Impl
    {
    public:
        FormulaImpl(SheetInterface& sheet_, std::string formula);
        CellType IGetType() const override;
        CellInterface::Value IGetValue() const override;    // Возвращает вычисленное значение формулы
        std::string IGetText() const override;              // Возвращает текст формулы ячейки (как для редактирования)

        std::vector<Position> IGetReferencedCells() const override;
        void IInvalidateCache() override;
        bool ICached() const override;
    private:
        SheetInterface& sheet_;    // Ссылка на лист таблицы (пробрасывается через конструктор Cell) для работы формул
        std::unique_ptr<FormulaInterface> formula_;
        std::optional<CellInterface::Value> cached_value_;
    };
};
