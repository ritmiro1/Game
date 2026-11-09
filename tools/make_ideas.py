# -*- coding: utf-8 -*-
"""Сборка каталога из 100 оригинальных 3D-концептов: JSON + Markdown."""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ideas_part1 import IDEAS_1
from ideas_part2 import IDEAS_2
from ideas_part3 import IDEAS_3

# Два концепта сняты как пересекающиеся по сути с уже включёнными
EXCLUDE = {"Тёплые Окна", "Паровая Прачечная"}

W = dict(visual=0.30, technical=0.30, gameplay=0.20, physics=0.20)

def build():
    raw = IDEAS_1 + IDEAS_2 + IDEAS_3
    ideas = [i for i in raw if i["name"] not in EXCLUDE]
    names = [i["name"] for i in ideas]
    assert len(names) == len(set(names)), "дубли имён: " + str([n for n in names if names.count(n) > 1])
    assert len(ideas) == 100, "концептов %d, нужно 100" % len(ideas)
    for n, i in enumerate(ideas, start=1):
        s = i["scores"]
        i["id"] = n
        i["weighted"] = round(sum(s[k] * W[k] for k in W), 2)
    ideas.sort(key=lambda x: (-x["weighted"], x["name"]))
    for rank, i in enumerate(ideas, start=1):
        i["rank"] = rank
        i["verdict"] = ("Победитель: к реализации" if rank == 1 else
                        "Фаворит: сильный кандидат" if rank <= 5 else
                        "Интересно, но дороже по срокам" if rank <= 20 else
                        "Концепт-резерв")
    return ideas

def md(ideas):
    L = []
    A = L.append
    A("# 100 оригинальных концептов 3D-игр — оценка и выбор №1")
    A("")
    A("Каталог составлен до начала производства. Все концепты **только 3D**: никаких 2D-проектов,")
    A("изометрических подделок и браузерных сборок. Оценка выставлялась по четырём критериям:")
    A("")
    A("| Критерий | Вес | Что оценивалось |")
    A("|---|---|---|")
    A("| Визуал | 30% | Насколько сцена читается и «продаёт» себя на скриншоте |")
    A("| Техника | 30% | Реализуемость без внешних ассетов и специального железа |")
    A("| Геймплей | 20% | Глубина цикла: есть ли чем заниматься на 10-м часу |")
    A("| Физика | 20% | Насколько «живой» мир: вода, верёвки, плавучесть, нагрузки |")
    A("")
    A("Итог = 0.30·Визуал + 0.30·Техника + 0.20·Геймплей + 0.20·Физика.")
    A("")
    A("## Итоговая таблица (все 100, по убыванию оценки)")
    A("")
    A("| № | Концепт | Жанр | Визуал | Техника | Геймплей | Физика | Итог |")
    A("|---:|---|---|---:|---:|---:|---:|---:|")
    for i in ideas:
        s = i["scores"]
        A("| %d | **%s** | %s | %.1f | %.1f | %.1f | %.1f | **%.2f** |" % (
            i["rank"], i["name"], i["genre"], s["visual"], s["technical"],
            s["gameplay"], s["physics"], i["weighted"]))
    A("")
    A("## Победитель")
    A("")
    win = ideas[0]
    A("### №1 — «%s» (%.2f балла)" % (win["name"], win["weighted"]))
    A("")
    A("**Жанр:** %s" % win["genre"])
    A("")
    A("%s" % win["idea"])
    A("")
    A("**Ядро цикла:** %s" % win["core"])
    A("")
    A("**Ключевые механики:**")
    for m in win["mechanics"]:
        A("- %s" % m)
    A("")
    A("**Визуальный язык:** %s" % win["visual"])
    A("")
    A("**Технические особенности:** %s" % win["tech"])
    A("")
    A("**Физика:** %s" % win["physics"])
    A("")
    A("Почему именно эта идея: она выигрывает по всем четырём весам одновременно. Визуал строится")
    A("на свете и воде, а не на количестве полигонов; техника реализуема целиком процедурно")
    A("(рельеф, растительность, звук) без сторонних ассетов; геймплей даёт внятную прогрессию")
    A("(снасти → виды рыбы → квесты → достижения); физика — это и есть игра: волны, натяжение")
    A("лески, плавучесть лодки. Полное ГДД — в файле `GDD_TikhiyZaliv.md`.")
    A("")
    A("## Концепты, вошедшие в топ-12")
    A("")
    for i in ideas[1:12]:
        s = i["scores"]
        A("### №%d — «%s» — %.2f" % (i["rank"], i["name"], i["weighted"]))
        A("*%s · В %.1f / Т %.1f / Г %.1f / Ф %.1f*" % (i["genre"], s["visual"], s["technical"],
                                                        s["gameplay"], s["physics"]))
        A("")
        A("%s" % i["idea"])
        A("Роль: %s. Опорная механика: %s" % (i["verdict"].lower(), i["core"]))
        A("")
    A("## Полные описания всех 100 концептов")
    A("")
    for i in ideas:
        s = i["scores"]
        A("### %d. «%s» — %.2f" % (i["rank"], i["name"], i["weighted"]))
        A("**Жанр:** %s  " % i["genre"])
        A("**Итог:** %.2f (В %.1f · Т %.1f · Г %.1f · Ф %.1f)" % (i["weighted"], s["visual"],
                                                                   s["technical"], s["gameplay"], s["physics"]))
        A("")
        A("%s" % i["idea"])
        A("")
        A("Ядро: %s" % i["core"])
        A("")
        A("Механики: %s" % "; ".join(i["mechanics"]))
        A("")
        A("Визуал: %s. Техника: %s. Физика: %s." % (i["visual"], i["tech"], i["physics"]))
        A("")
    return "\n".join(L) + "\n"

def main():
    ideas = build()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    out_json = os.path.join(root, "100_3D_GAME_IDEAS.json")
    out_md = os.path.join(root, "100_3D_GAME_IDEAS.md")
    data = {
        "title": "100 оригинальных концептов 3D-игр",
        "language": "ru",
        "total": len(ideas),
        "weights": W,
        "formula": "0.30*visual + 0.30*technical + 0.20*gameplay + 0.20*physics",
        "constraints": [
            "только 3D",
            "без внешних готовых ассетов",
            "реализуемо силами одного C-проекта",
            "интерфейс на русском языке"
        ],
        "selected": {"rank": 1, "name": ideas[0]["name"], "score": ideas[0]["weighted"],
                     "reason": "максимум по всем четырём весам при реализуемости без ассетов"},
        "ideas": ideas
    }
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    with open(out_md, "w", encoding="utf-8") as f:
        f.write(md(ideas))
    print("JSON:", out_json, os.path.getsize(out_json), "байт")
    print("MD:", out_md, os.path.getsize(out_md), "байт")
    print("Концептов:", len(ideas))
    for i in ideas[:5]:
        print("  %2d. %-34s %.2f" % (i["rank"], i["name"], i["weighted"]))

if __name__ == "__main__":
    main()
