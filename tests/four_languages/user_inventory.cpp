#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>

class Item {
private:
    int id;
    std::string name;
    double price;
    int quantity;
public:
    Item(int id, std::string name, double price, int quantity)
        : id(id), name(name), price(price), quantity(quantity) {}
    int getId() const { return id; }
    std::string getName() const { return name; }
    double getPrice() const { return price; }
    int getQuantity() const { return quantity; }
    void setPrice(double p) { price = p; }
    void setQuantity(int q) { quantity = q; }
    void display() const {
        std::cout << "| ID: " << std::setw(3) << id
                  << " | Name: " << std::setw(12) << name
                  << " | Price: $" << std::setw(6) << std::fixed << std::setprecision(2) << price
                  << " | Qty: " << std::setw(4) << quantity << " |\n";
    }
};

class Inventory {
private:
    std::vector<Item> items;
public:
    void addItem(const Item& item) {
        items.push_back(item);
        std::cout << "[INFO] Item added successfully.\n";
    }
    void removeItem(int id) {
        auto it = std::remove_if(items.begin(), items.end(), [id](const Item& item) {
            return item.getId() == id;
        });
        if (it != items.end()) {
            items.erase(it, items.end());
            std::cout << "[INFO] Item removed successfully.\n";
        } else {
            std::cout << "[ERROR] Item not found!\n";
        }
    }
    void displayInventory() const {
        std::cout << "\n--- CURRENT INVENTORY ---\n";
        if (items.empty()) {
            std::cout << "Inventory is empty.\n";
            return;
        }
        for (const auto& item : items) item.display();
        std::cout << "-------------------------\n";
    }
    double calculateTotalValue() const {
        double total = 0;
        for (const auto& item : items) total += item.getPrice() * item.getQuantity();
        return total;
    }
};

int main() {
    Inventory myStore;
    myStore.addItem(Item(101, "Laptop", 850.50, 5));
    myStore.addItem(Item(102, "Mouse", 25.99, 50));
    myStore.addItem(Item(103, "Keyboard", 45.00, 30));
    myStore.addItem(Item(104, "Monitor", 199.99, 12));
    myStore.addItem(Item(105, "Headset", 60.00, 20));
    myStore.displayInventory();
    std::cout << "Total Inventory Value: $" << myStore.calculateTotalValue() << "\n\n";
    std::cout << "Removing item ID 102...\n";
    myStore.removeItem(102);
    myStore.displayInventory();
    std::cout << "New Total Inventory Value: $" << myStore.calculateTotalValue() << "\n";
    return 0;
}
