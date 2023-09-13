import re
import warnings
from statistics import mode

# stop writing to my console!
warnings.filterwarnings('ignore')

import en_core_web_lg
import es_core_news_lg


def guess_organization(text: list, lang: str = "en") -> str:

    if (lang == "en"):
        nlp = en_core_web_lg.load()
    elif(lang == "es"):
        nlp = es_core_news_lg.load()

    # strip extra stuff which may confuse the nlp
    #text = list(re.sub(r'[\@\^\&\*\(\)\{\}\[\]\<\>\|\+\;]+|[\-\/\,\.\?\'\\]{2,3}|\b\w{1,3}\s\b', '', _).strip() for _ in text)


        # print(text, "\n")
        # for ent in (ent for ents in list(doc.ents for doc in nlp.pipe(text, disable=["tagger", "parser", "attribute_ruler", "lemmatizer"])) for ent in ents):
            # print(ent.label_, ": ", ent)
        # print("Choosing: ", end="")
        # return list(str(_) for _ in filter(lambda ent: ent.label_ == "ORG", (ent for ents in list(doc.ents for doc in nlp.pipe(text, disable=["tagger", "parser", "attribute_ruler", "lemmatizer"])) for ent in ents)))

    # we just return the most common (maybe there is a more accurate way?)
    return re.sub('\s+', ' ', str(mode(filter(lambda ent: ent.label_ == "ORG", (ent for ents in list(doc.ents for doc in nlp.pipe(text, disable=["tagger", "parser", "attribute_ruler", "lemmatizer"])) for ent in ents))))).lower().replace(' ', '-')
